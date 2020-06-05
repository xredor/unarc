extern "C" {
#include "../Compression.h"
}
#define MMD_LIBRARY

#ifndef FREEARC_DECOMPRESS_ONLY
#include "mmdet.cpp"

#ifndef MM_LIBRARY
#define print_message(s)   fprintf s
#else
#define print_message(s)
#endif


// PREPROCESSING ROUTINES *************************************************************************

// Run through buffer diffing 8-bit elements
void diff1 (void *buf, int bufsize, int N, void *_base)
{
    byte *base=(byte*)_base, x;
    for (byte *p=(byte*)buf; p+N<=(byte*)buf+bufsize; p+=N)
/*
        if (N==3) {
          int b=p[0], g=p[1], r=p[2];
          b-=g, r-=g;
          //int y = (r+2*g+b)/4; b-=g, r-=g; g=y;

			int tmp;

			g = r - b; // Co
			tmp = b + (p[1] >> 1);
			b = g - tmp; // Cg
			r = tmp + (p[0] >> 1); // Y

          p[0]=b-base[0], p[1]=g-base[1], p[2]=r-base[2];
          base[0]=b, base[1]=g, base[2]=r;



          int fb=p[0], y=p[1], fr=p[2];
          //fb+=fr*7/8, fr-=fb*17/32, fb+=y, y-=fb*3/8;
          fr-=y, y+=fr/2, fb-=y, y+=fb*3/8;
          p[0]=fb-base[0], p[1]=y-base[1], p[2]=fr-base[2];
          base[0]=fb, base[1]=y, base[2]=fr;
        } else
*/
        for (int i=0; i<N; i++)
            x=p[i], p[i]-=base[i], base[i]=x;
}

// Run through buffer diffing 16-bit elements
void diff2 (void *buf, int bufsize, int N, void *_base)
{
    uint16 *base=(uint16*)_base, x;
    for (uint16 *p=(uint16*)buf; p+N<=(uint16*)((char*)buf+bufsize); p+=N)
        for (int i=0; i<N; i++)
            x=p[i], p[i]-=base[i], base[i]=x;
}

// Run through buffer diffing 24-bit elements
void diff3 (void *buf, int bufsize, int N, void *_base)
{
    uint32 *base=(uint32*)_base; uint x;
    for (char *p=(char*)buf; p+N*3<=((char*)buf+bufsize); p+=N*3)
        for (int i=0; i<N; i++)
            x=value24(p+i*3), setvalue24(p+i*3, x-base[i]), base[i]=x;
}

// Run through buffer diffing 32-bit elements
void diff4 (void *buf, int bufsize, int N, void *_base)
{
    uint32 *base=(uint32*)_base, x;
    for (uint32 *p=(uint32*)buf; p+N<=(uint32*)((char*)buf+bufsize); p+=N)
        for (int i=0; i<N; i++)
            x=p[i], p[i]-=base[i], base[i]=x;
}

// Reorder buffer contents so that data for each byte of each channel are placed continuosly
BYTE* reorder_bytes (BYTE *buf, int bufsize, int N, int width)
{
    BYTE *newbuf = (BYTE*) malloc(bufsize);
    int X = N*width;
    for (int i=0; i<X; i++)
        for (int j=0; j<bufsize/X; j++)
            newbuf[i*bufsize/X+j] = buf[i+j*X];
    for (int i=bufsize-(bufsize%X); i<bufsize; i++)
        newbuf[i] = buf[i];
    memcpy (buf, newbuf, bufsize);
    free(newbuf);
    return buf;
}

// Reorder buffer contents so that each channel data are placed continuosly
BYTE* reorder_words (BYTE *buf, int bufsize, int N, int width)
{
    return buf;
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Run through buffer undiffing 8-bit elements
void undiff1 (void *buf, int bufsize, int N, void *_base)
{
    char *base=(char*)_base;
    for (char *p=(char*)buf; p+N<=(char*)buf+bufsize; p+=N)
        for (int i=0; i<N; i++)
            p[i] = base[i] += p[i];
}

// Run through buffer undiffing 16-bit elements
void undiff2 (void *buf, int bufsize, int N, void *_base)
{
    uint16 *base=(uint16*)_base;
    for (uint16 *p=(uint16*)buf; p+N<=(uint16*)((char*)buf+bufsize); p+=N)
        for (int i=0; i<N; i++)
            p[i] = base[i] += p[i];
}

// Run through buffer undiffing 24-bit elements
void undiff3 (void *buf, int bufsize, int N, void *_base)
{
    uint32 *base=(uint32*)_base;
    for (char *p=(char*)buf; p+N*3<=((char*)buf+bufsize); p+=N*3)
        for (int i=0; i<N; i++)
            base[i] += value24(p+i*3), setvalue24(p+i*3, base[i]);
}

// Run through buffer undiffing 32-bit elements
void undiff4 (void *buf, int bufsize, int N, void *_base)
{
    uint32 *base=(uint32*)_base;
    for (uint32 *p=(uint32*)buf; p+N<=(uint32*)((char*)buf+bufsize); p+=N)
        for (int i=0; i<N; i++)
            p[i] = base[i] += p[i];
}


// COMPRESSION METHOD IMPLEMENTATION **************************************************************

#ifndef FREEARC_DECOMPRESS_ONLY
// Multimedia detector and preprocessor
int mm_compress (int mode, int skip_header, int is_float, int num_chan, int word_size, int offset, int reorder, CALLBACK_FUNC *callback, void *auxdata)
{
    const int BUFSIZE = 1*mb;
    BYTE* buf = (BYTE*) malloc(BUFSIZE+1);  // buffer for data processed, 1 more byte for diff24 safeness
    void* base = NULL;                      // previous values for all channels what will be substracted from next ones

    int errcode,                            // код, возвращённый последней операцией чтения/записи
        bytes;                              // how many bytes was read to buf
    // Прочитаем первый мегабайт для детектирования типа мультимедии
    if ((errcode = bytes = callback ("read", buf, BUFSIZE, auxdata)) <= 0)  goto finished;   // temporary! BUFFER_SIZE will be enough

   {// Select parameters of check depending on speed mode setting
    int check_bytes = mymin (mode<=2? 64*kb : mymax(64*kb,bytes/2), bytes); // how many bytes to check
    int *use_channels  = mode<=2? fast_channels  : channels;
    int *use_bitvalues = mode<=2? fast_bitvalues : bitvalues;

    // If MM type isn't predefined and both file header recognition and
    // entropy autodetection fails then just copy data intact
    if (is_float || num_chan || word_size) {
        num_chan  || (num_chan=1);
        word_size || (word_size=is_float?32:8);
        print_message ((stdout, "User-defined: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
    } else if (!skip_header && autodetect_wav_header (buf, bytes, &is_float, &num_chan, &word_size, &offset)) {
        print_message ((stdout, "WAV header: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
    } else if (autodetect_by_entropy (buf+(bytes-check_bytes)/2, check_bytes, use_channels, use_bitvalues, 0.80, &is_float, &num_chan, &word_size, &offset)) {
        print_message ((stdout, "Entropy analyzer: is_float %d, num_chan %d, word_size %d, offset %d\n", is_float, num_chan, word_size, offset));
    } else {
        print_message ((stdout, "Autodetection failed: storing\n"));
        is_float = num_chan = word_size = offset = 0;
    }

    if (offset>bytes)   offset=bytes;    // otherwise we try to write negative amount of data :)
    int byte_size = (word_size+7)/8;     // bytes per word
    int N = num_chan*byte_size;          // bytes per sample
    int chunk =  roundDown(BUFSIZE,N);   // How many bytes should be processed each time
    int rest = bytes;
    bytes -= offset;                // First 'offset' bytes will be copied intact w/o any processing
    bytes = roundDown(bytes,N);     // Не обрабатывать первый блок данных целиком, если он содержит нецелое число отсчётов. Вместо этого оставшиеся байты будут включены во второй блок
    rest -= offset+bytes;           // how many bytes will remain unprocessed at end of buf
    base = calloc (num_chan, byte_size==3? 4:byte_size);   // previous values for all channels what will be substracted from next ones
    BYTE *ptr = buf + offset;       // In first block we should skip already copied data, in the following blocks ptr==buf

    // Write data header
    if (N==0) {
        byte header[1] = {0};
        WRITE (header, 1);         // Stored data are preceded with single '\0' byte
    } else {
        // Write header
        byte header[3];
        header[0] = 1+reorder*2;   // Flags byte: bit0 - diff, bit1 - reorder_bytes, bit2 - reorder_words
        header[1] = num_chan;
        header[2] = word_size;
        WRITE (header, 3);

        // Copy first few bytes intact in order to align diffX to start of 16/24/32-bit data samples
        WRITE4 (offset);
        WRITE  (buf, offset);

        // Write a few zero bytes in order to align output to position divisible by N
        WRITE  (base, roundUp(3+4+offset,N) - (3+4+offset));
    }

    // Цикл чтения/препроцессинга/записи блоков данных
    for(;;) {
        switch (byte_size) {
        case 1 : diff1 (ptr, bytes, num_chan, base);  break;
        case 2 : diff2 (ptr, bytes, num_chan, base);  break;
        case 3 : diff3 (ptr, bytes, num_chan, base);  break;
        case 4 : diff4 (ptr, bytes, num_chan, base);  break;
        }
        switch (reorder) {
        case 1 : reorder_bytes (ptr, bytes, num_chan, byte_size);  break;
        case 2 : reorder_words (ptr, bytes, num_chan, byte_size);  break;
        }
        WRITE (ptr, bytes);

        // Move unprocessed rest of data into buffer beginning
        memmove (buf, ptr+bytes, rest);  ptr=buf;

        // Starting from second iteration, we will process data in 64k chunks
        chunk = N>1? roundDown(BUFFER_SIZE,N) : BUFFER_SIZE;
        buf   = (BYTE*) realloc (buf, chunk+1);       // +1 is for processing 24-bit chunks using 32-bit operations

        // Read next data chunk. This time, bytes+rest should be exactly == chunk, unless we at the end of data
        errcode = bytes = callback ("read", buf+rest, chunk-rest, auxdata);
        if (errcode<0) break;                // break on error
        bytes += rest;  rest=0;
        if (bytes==0) break;                 // break on EOF
    }}

finished:
    FreeAndNil(base);
    FreeAndNil(buf);
    return errcode;         // 0, если всё в порядке, и код ошибки иначе
}
#endif  // !defined (FREEARC_DECOMPRESS_ONLY)


int mm_decompress (CALLBACK_FUNC *callback, void *auxdata)
{
    byte header[3];
    BYTE* buf  = (BYTE*) malloc (BUFFER_SIZE+1);  // buffer for data processed
    void* buf1 = NULL;
    void* base = NULL;                            // room for previous values for all channels what will be substracted from next ones
    int errcode,                                  // код, возвращённый последней операцией чтения/записи
        bytes;                                    // how many bytes was read to buf

    READ (header,1);
    if (header[0] == 0) {
        // Copy data in chunks
        while ( (errcode = bytes = callback ("read", buf, BUFFER_SIZE, auxdata)) > 0 )
        {
            WRITE (buf, bytes);
        }
    } else {
        // Check that bits other than bit0-bit2 are not set (these should be used for future extensions)
        // Well, just now we don't support even bits1&2 enabled (i.e. we don't support restoring proper byte order)
        if (header[0] & ~1)  {errcode=FREEARC_ERRCODE_BAD_COMPRESSED_DATA; goto finished;}
        // Read the remaining two header bytes
        READ (header+1, 2);

        // Copy original file header
        int offset;
        READ4 (offset);
        buf1 = malloc(offset);
        READ (buf1, offset);
        WRITE (buf1, offset);
        FreeAndNil (buf1);

        //int reorder   = header[0]/2;
        int num_chan  = header[1];                    // number of channels
        int word_size = header[2];                    // size of one sample in one channel, in bits
        int byte_size = (word_size+7)/8;              // bytes per word
        int N         = num_chan*byte_size;           // bytes per sample
        int chunk     = roundDown (BUFFER_SIZE, N);   // size of data processing chunks
        base = calloc (num_chan, byte_size==3? 4:byte_size);  // room for previous values for all channels what will be substracted from next ones
        READ (base, roundUp(3+4+offset,N) - (3+4+offset));  // skip alignment bytes

        // Process data in chunks
        while ( (errcode = bytes = callback ("read", buf, chunk, auxdata)) > 0 )
        {
            switch (byte_size) {
            case 1 : undiff1 (buf, bytes, num_chan, base);  break;
            case 2 : undiff2 (buf, bytes, num_chan, base);  break;
            case 3 : undiff3 (buf, bytes, num_chan, base);  break;
            case 4 : undiff4 (buf, bytes, num_chan, base);  break;
            }                     /*
            switch (reorder) {
            case 1 : unreorder_bytes (buf, bytes, num_chan, byte_size);  break;  // temporary!
            case 2 : unreorder_words (buf, bytes, num_chan, byte_size);  break;
            }             */
            WRITE (buf, bytes);
        }
    }

finished:
    FreeAndNil (base);
    FreeAndNil (buf1);
    FreeAndNil (buf);
    return errcode;         // 0, если всё в порядке, и код ошибки иначе
}



#ifndef MM_LIBRARY
// DRIVER ************************************************************************
// This demo program shows how to use MM preprocessor

#include "../Common.cpp"

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

    if (show_stat && size>1000) {
        ERASE_STDERR;
        if ( !unpack ) {
            fprintf (stdout, "Encode:  wrote %.0f bytes, %.0f%% complete, time: %d\r",
                (double) output_byte_count,
                (double) input_byte_count/ (data_size + 1) * 100,
                (int) (time (NULL) - stime));
        } else {
            fprintf (stdout, "Decode:  wrote %.0f bytes, %.0f%% complete, time: %d\r",
                (double) output_byte_count,
                (double) input_byte_count/(data_size + 1) * 100,
                (int) (time (NULL) - stime));
        }
    }
    return size;
}

// Разбор командной строки, чтение входных данных, вызов encode/decode, и запись выходных данных
int main (int argc, char **argv)
{
    int mode        = 9;  // Detection speed mode (1 - fastest, 9 - most accurate)
    int skip_header = 0;  // Skip file header detection
    int is_float    = 0;  // Floating-point data format
    int num_chan    = 0;  // Channels count
    int word_size   = 0;  // Size of each encoded value, in bits
    int offset      = 0;  // File offset where MM data start (header is copied intact)
    int reorder     = 0;  // Reorder bytes/words

    while (argc>1) {
    	if (argv[1][0] == '-') {
            switch( tolower(argv[1][1]) ) {
                case 'd':   if (argv[1][2])   mode = atoi(argv[1]+2);
                            else              unpack++;  break;
                case 's':   skip_header++;               break;
                case 'f':   is_float++;                  break;
                case 'c':   num_chan  = atoi(argv[1]+2); break;
                case 'w':   word_size = atoi(argv[1]+2); break;
                case 'o':   offset    = atoi(argv[1]+2); break;
                case 'r':   reorder   = atoi(argv[1]+2); break;
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
        printf( "\n Usage: mm [options] original-file [packed-file]");
        printf( "\n   -d# -- detection speed mode (1 - fastest, 9 - most accurate)");
        printf( "\n   -s  -- skip WAV header detection");
        printf( "\n   -f  -- floating-point data format");
        printf( "\n   -c# -- channels count");
        printf( "\n   -w# -- word size, in bits (8/16)");
        printf( "\n   -o# -- offset of MM data in file (=header size)");
        printf( "\n   -r# -- reorder data. -r1 - reorder bytes, -r2 - reorder words (unfinished!)");
        printf( "\n   c*w -- use c channels w bits each (example: 3*8)");
        printf( "\n   o+c*w -- use c channels w bits each starting from offset o");
        printf( "\n" );
        printf( "\n For decompress: mm -d packed-file [unpacked-file]");
        printf( "\n" );
        exit(1);
    }
    fin = fopen (argv[1], "rb");
    if (fin == NULL) {
        printf( "\n Can't open %s for read\n", argv[1]);
        exit(2);
    }

    // Записать выходные данные, если был указан выходной файл
    fout = fopen (argc==3? argv[2] : "NUL", "wb");
    if (fout == NULL) {
        printf ("\n Can't open %s for write\n", argv[2]);
        exit(3);
    }

    // clear statistics
    input_byte_count = output_byte_count = 0;
    stime = time(NULL);
    data_size = filelength(fileno(fin));

    // Произвести упаковку или распаковку
    !unpack
      ? mm_compress   (mode, skip_header, is_float, num_chan, word_size, offset, reorder, readFILE, writeFILE)
      : mm_decompress (readFILE, writeFILE);

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

#endif  // !defined (MM_LIBRARY)
