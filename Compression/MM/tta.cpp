/*
 * ttaenc.c
 *
 * Description:  TTA lossless audio encoder/decoder.
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

#ifdef _WIN32
#pragma  pack(1)
#endif

#include "../Compression.h"
#include "ttaenc.h"
#include "entropy.h"
#include "filters.h"
#include "mmdet.h"

#ifndef TTA_LIBRARY
#define print_message(s)   fprintf s
#else
#define print_message(s)
#endif

// Number of channels that wave file may have. Used for auto-detection when
// WAV header doesn't exist or don't taken into account. This array is zero-ended
static int channels[] = {1,2,0};

// Number of bits per word that wave file may have. Used for auto-detection, too
static int bitvalues[] = {8,16,0};


void tta_error (long error, const char *name)
{
    switch (error) {
    case COMMAND_ERROR:
        fprintf (stderr, "Error:   unknown command '%s'\n%s\n", name, LINE); break;
    case FORMAT_ERROR:
        fprintf (stderr, "Error:   not compatible file format\n%s\n", LINE); break;
    case FIND_ERROR:
        fprintf (stderr, "Error:   file(s) not found '%s'\n%s\n\n", name, LINE); exit(1);
    case CREATE_ERROR:
        fprintf (stderr, "Error:   problem creating directory '%s'\n%s\n\n", name, LINE); exit(1);
    case OPEN_ERROR:
        fprintf (stderr, "Error:   can't open file '%s'\n%s\n\n", name, LINE); exit(1);
    case FILE_ERROR:
        fprintf (stderr, "\nError:   file is corrupted\n%s\n", LINE); break;
    case WRITE_ERROR:
        fprintf (stderr, "\nError:   can't write to output file\n%s\n\n", LINE); exit(1);
    case READ_ERROR:
        fprintf (stderr, "\nError:   can't read from input file\n%s\n\n", LINE); exit(1);
    case MEMORY_ERROR:
        fprintf (stderr, "\nError:   insufficient memory available\n%s\n\n", LINE); exit(1);
    }
}

void *malloc1d (size_t num, size_t size)
{
    void    *array;

    if ((array = calloc (num, size)) == NULL)
        tta_error (MEMORY_ERROR, NULL);

    return (array);
}

long **malloc2d (long num, unsigned long len)
{
    long    i, **array, *tmp;

    array = (long **) calloc (num, sizeof(long *) + len * sizeof(long));
    if (array == NULL) tta_error (MEMORY_ERROR, NULL);

    for(i = 0, tmp = (long *) array + num; i < num; i++)
        array[i] = tmp + i * len;

    return (array);
}

#ifndef FREEARC_DECOMPRESS_ONLY
static long read_wave (long *data, void *rest, void **bufferptr, void *prevbuf, long prevsize, long byte_size, long num_chan, unsigned long len, CALLBACK_FUNC *callback, void *auxdata)
{
    long i, rest_bytes, elements;
    char *buffer = (char*) malloc1d (len + 2, num_chan*byte_size);
    long wanted = len*num_chan*byte_size;
    long use_prevsize = mymin(prevsize,wanted);
    memcpy (buffer, prevbuf, use_prevsize);
    long bytes_read =  wanted <= prevsize?  0  :  callback ("read", buffer+prevsize, wanted-prevsize, auxdata);

    if (bytes_read >= 0) {  // If read ok
        bytes_read += use_prevsize;
        rest_bytes = bytes_read%(num_chan*byte_size);
        memcpy (rest, buffer+bytes_read-rest_bytes, rest_bytes);
        elements = (bytes_read/(num_chan*byte_size)) * num_chan;

        switch (byte_size) {
        case 1: {
                    unsigned char *sbuffer = (unsigned char *)buffer;
                    for (i = 0; i < elements; i++)
                        data[i] = (long) sbuffer[i] - 0x80;
                    break;
                }
        case 2: {
                    short *sbuffer = (short*)buffer;
                    for (i = 0; i < elements; i++)
                        data[i] = (long) sbuffer[i];
                    break;
                }
        case 3: {
                    unsigned char *sbuffer = (unsigned char *)buffer;
                    for (i = 0; i < elements; i++) {
                        unsigned long t = *((long *)(sbuffer + i * byte_size));
                        data[i] = (long) (t << 8) >> 8;
                    }
                    break;
                }
        case 4: {
                    long *sbuffer = (long*)buffer;
                    for (i = 0; i < elements; i++)
                        data[i] = sbuffer[i];
                    break;
                }
        }
    }
    *bufferptr = buffer;
    return (bytes_read);
}
#endif

static long write_wave (long **data, long byte_size, long num_chan, unsigned long len, CALLBACK_FUNC *callback, void *auxdata)
{
    long    n;
    long    i, res;
    void    *buffer;

    buffer = malloc1d (len * num_chan + 2, byte_size);

    switch (byte_size) {
    case 1: {
                unsigned char *sbuffer = (unsigned char *)buffer;
                for (i = 0; i < (len * num_chan); i+= num_chan)
                for (n = 0; n < num_chan; n++) sbuffer[i+n] = (unsigned char) (data[n][i/num_chan] + 0x80);
                break;
            }
    case 2: {
                short *sbuffer = (short*)buffer;
                for (i = 0; i < (len * num_chan); i+= num_chan)
                for (n = 0; n < num_chan; n++) sbuffer[i+n] = (short) data[n][i/num_chan];
                break;
            }
    case 3: {
                unsigned char *sbuffer = (unsigned char *)buffer;
                for (i = 0; i < (len * num_chan); i+= num_chan)
                for (n = 0; n < num_chan; n++)
                    *((long *)(sbuffer + (i+n) * byte_size)) = data[n][i/num_chan];
                break;
            }
    case 4: {
                long *sbuffer = (long*)buffer;
                for (i = 0; i < (len * num_chan); i+= num_chan)
                for (n = 0; n < num_chan; n++) sbuffer[i+n] = data[n][i/num_chan];
                break;
            }
    }

    res = callback ("write", buffer, byte_size * len * num_chan, auxdata);
    free (buffer);
    return res;
}

#ifndef FREEARC_DECOMPRESS_ONLY
void split_int (long *data, long frame_len, long num_chan, long **buffer)
{
    long    i, j, n;

    for (i = 0; i < frame_len; i++)
    for (j = 0; j < num_chan; j++) {
        buffer[j][i] = data[i * num_chan + j];
    }

    if (num_chan > 1)
    for (i = 0, n = (num_chan - 1); i < frame_len; i++) {
        for (j = 0; j < n; j++)
            buffer[j][i] = buffer[j+1][i] - buffer[j][i];
        buffer[n][i] = buffer[n][i] - (buffer[n-1][i] / 2);
    }
}
#endif

void combine_int (long frame_len, long num_chan, long **buffer)
{
    long    i, j, n;

    if (num_chan > 1)
    for (i = 0, n = (num_chan - 1); i < frame_len; i++) {
        buffer[n][i] = buffer[n][i] + (buffer[n-1][i] / 2);
        for (j = n; j > 0; j--)
            buffer[j-1][i] = buffer[j][i] - buffer[j-1][i];
    }
}

#ifndef FREEARC_DECOMPRESS_ONLY
void split_float (long *data, long frame_len, long num_chan, long **buffer)
{
    long    i, j;

    for (i = 0; i < frame_len; i++)
    for (j = 0; j < num_chan; j++) {
        unsigned long t = data[i * num_chan + j];
        unsigned long negative = (t & 0x80000000)? -1:1;
        unsigned long data_hi = (t & 0x7FFF0000) >> 16;
        unsigned long data_lo = (t & 0x0000FFFF);

        buffer[j][i] = data_hi - 0x3F80;
        buffer[j+num_chan][i] = (SWAP16(data_lo) + 1) * negative;
    }
}
#endif

void combine_float (long frame_len, long num_chan, long **buffer)
{
    long    i, j;

    for (i = 0; i < frame_len; i++)
    for (j = 0; j < num_chan; j++) {
        unsigned long negative = buffer[j+num_chan][i] & 0x80000000;
        unsigned long data_hi = buffer[j][i];
        unsigned long data_lo = abs(buffer[j+num_chan][i]) - 1;

        data_hi += 0x3F80;
        buffer[j][i] = (data_hi << 16) | SWAP16(data_lo) | negative;
    }
}


#ifndef FREEARC_DECOMPRESS_ONLY
int tta_compress (int level, int skip_header, int is_float, int num_chan, int word_size, int offset, int raw_data, CALLBACK_FUNC *callback, void *auxdata)
{
    long            *data=NULL, **buffer=NULL;
    void            *rest=NULL, *origdata=NULL;
    char            *prevptr=NULL, *prevbuf=NULL;
    unsigned long   i, j, bytes_read, prevsize=0;
    unsigned long   frame_size, frame_len, bit_array_size;
    unsigned char   header[4];
    int             errcode, byte_size;
    bit_array_write = NULL;
    level = mymin (level, 3);

    // Size of each chunk processed, in samples (num_chan*byte_size bytes each)
    frame_size = 1<<18;

    // Auto-detect settings for is_float/num_chan/word_size/offset unless any of these are explicitly specified
    if (level==0) {
        goto storing;
    } else if (is_float || num_chan || word_size) {
        num_chan  || (num_chan=1);
        word_size || (word_size=is_float?4:1);
        print_message ((stdout, "User-defined: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
    } else {
        // Read first 1mb of data for MM type autodetection
        prevbuf = prevptr = (char*)malloc1d (1*mb, 1);
        READ_LEN_OR_EOF (prevsize, prevbuf, 1*mb);

        // If both WAV file header and entropy autodetection fails use default values
        if (!skip_header && autodetect_wav_header (prevbuf, prevsize, &is_float, &num_chan, &word_size, &offset)) {
            print_message ((stdout, "WAV header: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
        } else if (autodetect_by_entropy ((char*)prevbuf, prevsize, channels, bitvalues, 0.50, &is_float, &num_chan, &word_size, &offset)) {
            print_message ((stdout, "Entropy analyzer: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
        } else  {
            print_message ((stdout, "Autodetection failed: storing\n"));
storing:
            //printf ("storing\n");
            // Write header denoting stored data and then copy whole input to output
            WRITE4 (0);
            while (1) {
                if (prevbuf != NULL)   {WRITE (prevbuf, prevsize)}
                else                   prevbuf = (char*)malloc1d (1*mb, 1);
                READ_LEN_OR_EOF (prevsize, prevbuf, 1*mb);
            }
        }
    }
    //printf ("Data detected: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset);
    byte_size = (word_size+7)/8;                    // bytes per word

    // TTA doesn't support 32+ bit integers and non-32-bit floats
    if ((is_float && byte_size != 4) ||
       (!is_float && byte_size >= 4))
           goto storing;

    // Write data header
    header[0] = level;
    header[1] = raw_data*2 + is_float;
    header[2] = num_chan;
    header[3] = word_size;
    WRITE (header, 4);
    //printf ("header ok\n");

    // Copy header of original data that doesn't need to be compressed
    if (offset>0 && prevptr==NULL) {   // If header isn't yet in prevbuf - read it
        prevbuf  = prevptr = (char*)malloc1d (offset, 1);
        READ (prevbuf, offset);
        prevsize = offset;
    }
    WRITE4 (offset);
    WRITE  (prevptr, offset);
    prevptr+=offset; prevsize-=offset;  // Exclude these data from further processing
    //printf ("offset ok\n");

    // grab some space for buffers
    data = (long *) malloc1d (num_chan * frame_size, sizeof (long));
    rest = malloc1d (num_chan, byte_size);
    buffer = malloc2d (num_chan << is_float, frame_size);
    //printf ("malloc ok\n");

    while (1) {
        // Read next input block and convert it into long values
        bytes_read = read_wave (data, rest, &origdata, prevptr, prevsize, byte_size, num_chan, frame_size, callback, auxdata);
        frame_len = bytes_read/(num_chan*byte_size);
        if (bytes_read>=prevsize) {
            FreeAndNil(prevbuf); prevsize=0;  // Now prevbuf (data read for autodetection) is fully consumed
        } else {
            prevptr+=bytes_read; prevsize-=bytes_read;  // There are still (prevptr,prevsize) data to read in prevbuf
        }
        if ((errcode=bytes_read) <= 0)  goto finished;   // Leave loop on EOF or error reading data
        //printf ("Data read\n");

        // Write block header
        WRITE4 (bytes_read);

        if (is_float)   split_float (data, frame_len, num_chan, buffer);
        else            split_int   (data, frame_len, num_chan, buffer);
        //printf ("Data split\n");

        init_bit_array_write ();

        // compress block
        for (i = 0; i < (num_chan << is_float); i++) {
            filters_compress (buffer[i], frame_len, level, byte_size);
            //printf ("Data filtered\n");

            if (!raw_data) {
                encode_frame (buffer[i], frame_len);
            } else {
                if (raw_data==2)
                    // Convert signed values to unsigned ones
                    for (j = 0; j < frame_len; j++) {
                        long t = buffer[i][j];
                        buffer[i][j] =  t>=0 ? t*2 : (-t)*2-1;
                    }
                WRITE (buffer[i], frame_len*sizeof(long));
            }
        }
        //printf ("Data encoded\n");

        if (!raw_data) {
            bit_array_size = get_len ();
            if (bit_array_size >= bytes_read) {
                WRITE4 (0);                           // incompressible - store original data instead
                WRITE  (origdata, bytes_read);
                goto skip_writing_rest;
            } else {
                WRITE4 (bit_array_size);              // write compressed data
                WRITE  (bit_array_write, bit_array_size);
            }
        }
        WRITE (rest, bytes_read%(num_chan*byte_size));
skip_writing_rest:
        FreeAndNil (bit_array_write);
        FreeAndNil (origdata);
    }

finished:
    FreeAndNil (bit_array_write);
    FreeAndNil (origdata);
    FreeAndNil (buffer);
    FreeAndNil (rest);
    FreeAndNil (data);
    FreeAndNil (prevbuf);

    return errcode;
}
#endif

int tta_decompress (CALLBACK_FUNC *callback, void *auxdata)
{
    long    **buffer=NULL;
    void    *rest=NULL;
    void    *buf1=NULL;
    void    *prevbuf=NULL;
    unsigned long   i, level, raw_data, num_chan, word_size, byte_size, bytes_read, offset;
    unsigned long   is_float, frame_len, bit_array_size;
    unsigned char   header[4];
    int errcode;
    bit_array_read = NULL;

    // read TTA header
    READ (header, 4)
    level      = header[0];
    is_float   = header[1]%2;
    raw_data   = header[1]/2;
    num_chan   = header[2];
    word_size  = header[3];
    byte_size  = (word_size+7)/8;                    // bytes per word

    // Return error if the header requests any (yet) unsupported features
    if (level    > 3                 ||
        raw_data > 1                 ||
        (is_float && byte_size != 4) ||
       (!is_float && byte_size >= 4))     return FREEARC_ERRCODE_BAD_COMPRESSED_DATA;

    // If TTA header signals that this is stored data, then just copy input to output
    if (level==0) {
        prevbuf = malloc1d (1*mb, 1);
        while (1) {
            int prevsize;
            READ_LEN_OR_EOF (prevsize, prevbuf, 1*mb);
            WRITE (prevbuf, prevsize);
        }
    }

    // Copy header of original data that isn't compressed
    READ4 (offset);
    buf1 = malloc(offset);
    READ (buf1, offset);
    WRITE(buf1, offset);
    FreeAndNil (buf1);

    rest = malloc1d (num_chan, byte_size);

    while (1) {
        // read block header which stores uncompressed size of block
        READ4 (bytes_read);
        if (bytes_read > (1 << 30))
            tta_error (FILE_ERROR, NULL);
        frame_len = bytes_read/(num_chan*byte_size);

        // read block data
        if (!raw_data) {
            // Read second part of block header which stores *compressed* size of block
            READ4 (bit_array_size);
            if (bit_array_size > (1 << 30))
                tta_error (FILE_ERROR, NULL);
            if (bit_array_size == 0) {               // This is a stored block:
                prevbuf = malloc1d (bytes_read, 1);  //   copy bytes_read bytes
                READ  (prevbuf, bytes_read);         //     from input stream
                WRITE (prevbuf, bytes_read);         //     to output stream
                FreeAndNil (prevbuf);                //
                continue;                            //   and go to next block
            }
            init_bit_array_read (bit_array_size);
            READ (bit_array_read, bit_array_size);
        }

        // grab some space for a buffer
        buffer = malloc2d (num_chan << is_float, frame_len);

        for (i = 0; i < (num_chan << is_float); i++) {
            if (raw_data) {
                READ (buffer[i], frame_len*sizeof(long));
            } else {
                decode_frame (buffer[i], frame_len);
            }
            filters_decompress (buffer[i], frame_len, level, byte_size);
        }

        if (is_float)   combine_float (frame_len, num_chan, buffer);
        else            combine_int   (frame_len, num_chan, buffer);

        FreeAndNil (bit_array_read);
        if (frame_len) {
            errcode = write_wave (buffer, byte_size, num_chan, frame_len, callback, auxdata);
            if (errcode<0)  goto finished;
        }
        FreeAndNil (buffer);

        // Copy bytes at end of file
        READ  (rest, bytes_read%(num_chan*byte_size));
        WRITE (rest, bytes_read%(num_chan*byte_size));
    }
finished:
    FreeAndNil (prevbuf);
    FreeAndNil (buf1);
    FreeAndNil (bit_array_read);
    FreeAndNil (buffer);
    FreeAndNil (rest);
    return errcode;
}


#ifndef TTA_LIBRARY
// DRIVER ************************************************************************
// This demo program shows how to use TTA sound wave compressor

// to do:
// 1. -w4 - error!!! the same for other encodings where compression really doesn't work
// 2. 12*0 - stored file, compression params in each block, TTA header for grzip algo
// 3. cobalp?
// 4. class Buffer?

// ХОЗЧАСТЬ ************************************************************************
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <windows.h>

#include "../Compression.h"
#include "../Common.cpp"
#include "ttaenc.h"

#ifdef _WIN32
    #define ERASE_STDERR fprintf (stderr, "%78s\r", "")
#else
    #define ERASE_STDERR fprintf (stderr, "\033[2K")
#endif
#define LINE "------------------------------------------------------------"



FILE       *fin, *fout;
time_t     stime;
uint64     input_byte_count;
uint64     output_byte_count;
int        show_stat = 1;
int        unpack = 0;    // Unpack previously compressed data
uint64     data_size;     // Input file size

int readFILE (/*void* param,*/ void* buf, int size)
{
    //FILE *fin = (FILE*)param;
    int res = read (fin, buf, size);
    if (res>0)  input_byte_count += res;
    return res;
}

int writeFILE (/*void* param,*/ void* buf, int size)
{
    if (output_byte_count==0) {
        // print process banner
        !unpack
          ? fprintf (stdout, "Encode:  processing ..\r")
          : fprintf (stdout, "Decode:  processing ..\r");
    }

    //FILE *fout = (FILE*)param;
    write (fout, buf, size);
    output_byte_count += size;

    if (show_stat && size>4) {
        ERASE_STDERR;
        if ( !unpack ) {
            fprintf (stdout, "Encode:  wrote %.0f bytes, %.0f%% complete, ratio: %.2f, time: %d\r",
                (double) output_byte_count,
                (double) input_byte_count/ (data_size + 1) * 100,
                (double) output_byte_count/(input_byte_count + 1),
                (int) (time (NULL) - stime));
        } else {
            fprintf (stdout, "Decode:  wrote %.0f bytes, %.0f%% complete, ratio: %.2f, time: %d\r",
                (double) output_byte_count,
                (double) input_byte_count/(data_size + 1) * 100,
                (double) output_byte_count/(input_byte_count + 1),
                (int) (time (NULL) - stime));
        }
    }
    return 0;
}

// Разбор командной строки, чтение входных данных, вызов encode/decode, и запись выходных данных
int main (int argc, char **argv)
{
    int tta_level   = 3;  // Compression level (1..3, higher means tighter and slower compression)
    int skip_header = 0;  // Skip file header detection
    int is_float    = 0;  // Floating-point data format
    int num_chan    = 0;  // Channels count
    int word_size   = 0;  // Size of each encoded value, in bits
    int offset      = 0;  // File offset where MM data start (header is copied intact)
    int raw_data    = 0;  // Write raw predictor's output without using entropy encoder

    while (argc>1) {
    	if (argv[1][0] == '-') {
            switch( tolower(argv[1][1]) ) {
                case 'd':   unpack++;                    break;
                case 'm':   tta_level = atoi(argv[1]+2); break;
                case 's':   skip_header++;               break;
                case 'f':   is_float++;                  break;
                case 'c':   num_chan  = atoi(argv[1]+2); break;
                case 'w':   word_size = atoi(argv[1]+2); break;
                case 'o':   offset    = atoi(argv[1]+2); break;
                case 'r':   raw_data  = atoi(argv[1]+2); break;
                default :   printf( "\n Unknown option '%s'\n", argv[1]);
                            exit(1);
            }
        } else {
            int a, b, c;
            if (sscanf (argv[1], "%d+%d*%d", &a, &b, &c)==3)
                offset=a, num_chan=b, word_size=c, is_float=0;
            else if (sscanf (argv[1], "%d*%d", &a, &b)==2)
                num_chan=a, word_size=b, is_float=0;
            else break;
        }
        argv++, argc--;
    }

    if (argc != 2  &&  argc != 3) {
        printf( "\n Usage: tta [options] original-file [packed-file]");
        printf( "\n   -m# -- compression level [1..3], default %d", tta_level);
        printf( "\n   -s  -- skip WAV header detection");
        printf( "\n   -f  -- floating-point data format");
        printf( "\n   -c# -- channels count");
        printf( "\n   -w# -- word size, in bits (8/16)");
        printf( "\n   -o# -- offset of MM data in file (=header size)");
        printf( "\n   -r# -- output raw data (no entropy coder). -r1/-r2");
        printf( "\n   c*w -- use c channels w bits each (example: 3*8)");
        printf( "\n   o+c*w -- use c channels w bits each starting from offset o");
        printf( "\n" );
        printf( "\n For decompress: tta -d packed-file [unpacked-file]");
        printf( "\n" );
        exit(1);
    }
    fin = fopen( argv[1], "rb" );
    if (fin == NULL) {
        printf( "\n Can't open %s for read\n", argv[1]);
        exit(2);
    }

    // Записать выходные данные, если был указан выходной файл
    fout = fopen( argc==3? argv[2] : "NUL", "wb" );
    if (fout == NULL) {
        printf( "\n Can't open %s for write\n", argv[2]);
        exit(3);
    }

    // clear statistics
    input_byte_count = output_byte_count = 0;
    stime = time(NULL);
    data_size = filelength(fileno(fin));

    // Произвести упаковку или распаковку
    !unpack
      ? tta_compress   (tta_level, skip_header, is_float, num_chan, word_size, offset, raw_data, readFILE, fin, writeFILE, fout)
      : tta_decompress (readFILE, fin, writeFILE, fout);

    // Print final stats
    ERASE_STDERR;

    if ( !unpack ) {
        fprintf (stdout, "Encode:  wrote %d bytes, done, ratio: %.2f, time: %d\n",
            (int) output_byte_count,
            (float) output_byte_count/(input_byte_count + 1),
            (int) (time (NULL) - stime));
    } else {
        fprintf (stdout, "Decode:  wrote %d bytes, done, ratio: %.2f, time: %d\n",
            (int) (output_byte_count),
            (float) output_byte_count/(input_byte_count + 1),
            (int) (time (NULL) - stime));
    }
    fprintf (stdout, "%s\n", LINE);

    return 0;
}

#endif

