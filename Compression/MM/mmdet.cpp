// Public domain code for MM data detection algorithm.
// (c) Bulat Ziganshin <Bulat.Ziganshin@gmail.com>

/* The following code snippet shows how to use the library.
   Please note that you need to try only models that your program
   really want to explore. For example, you may try only models using diff_run
   if your program doesn't support models without substraction of successive
   elements, or you may skip entirely checking 24-bit models.
   Also, you may skip trying some models based on results of earlier ones
   (say, probably, there is no need for 3*8 if 2*16 already shows good results).

        // Maximum number of channels we are trying to use
        int MAX_CHANNELS = 3;

        // Best model so far
        Model best_model;
        best_model.result = LONG_MAX;

        // Calc stats for various number of channels, bitwidth and enabled/disabled diff'ing
        for (int diff=0; diff<=1; diff++)
            for (int N=1; N<=MAX_CHANNELS; N++)
                for (int bits=8; bits<=32; bits+=8) {
                    // Try model with given parameters
                    Model model (buf, filesize);
                    diff?  model.diff_run (N, bits)
                        :  model.run (N, bits);

                    // Select best model so far
                    if (model.result < best_model.result)   best_model = model;
                }

        // Collect stats for order-0 model for comparision
        Model order0 (buf, filesize);
        order0.run_order0();

        // Use MM compression if it provides at least 5% better compression than order-0 model
        if (best_model.result < order0.result*0.95)
            // Print parameters of best model detected
            printf ("BEST model is %d channels * %d bits, %s (%3.1f%% compared to order-0 model)",
                     best_model.channels,
                     best_model.bitwidth,
                     best_model.diff? "diffed" : "not diffed",
                     (double)best_model.result*100/order0.result);
        else
            printf ("MM compression? ha-ha, forget about this!");

*/

// to do:
// 0. detect "bad cases" such as rafale.bmp which don't like MM compression
//        Details: http://www.encode.ru/forums/index.php?action=vthread&forum=1&topic=391
// 1. floating-point values detection
// 2. optimizations: specific loops for each N (done for 8/16 bits), fast long->ulong conversion
// 3. provide additional code to detect signed vs unsigned data, MM vs data tables,
//        left/right vs mid/diff channels and so on
// 4. image width detection

#include "Compression.h"
extern "C" {
#include "mmdet.h"
}

// MAIN ALGORITHM ************************************************************************
// The algorithm is straightforward - calculate compressed size
// using order-0 model for 8/16/24/32 bit values
// (for 16-32 bits we group values in slots).
// Then repeat the calculation after MM substraction processing with
// varying word length (8-32) and number of channels.
// Smallest result points to the best model

// Number of entries for each channel in stats table
#define STATSIZE 1024

// Read unsigned 24-bit value at given address
#define unsigned24bit     value24

// Read signed 24-bit value at given address. NB! This may fail on 64-bit cpus :(
#define signed24bit(p)    ((*(sint32*)(p) << 8) >> 8)


// Model of data compression algorithm which estimates it's compression results
struct Model
{                                       // Model's parameters:
    int            channels;            //     Number of channels
    int            bitwidth;            //     Size of each element, in bits
    int            offset;              //     Offset of first sample relative to buffer start, in bytes
    int            diff;                //     Whether successive elements in each channel substracted from each other
                                        // Results shown by the model:
    double         bits;                //     Bits used to encode small values and number of range for large values
    unsigned long  xbits;               //     Extra bits (used for encoding of values not in -128..127 range)
    long           result;              //     Total bytes

    void           *buf;                // Buffer's address and size for easiness of
    unsigned       bufsize;             //     multiple runs through the same buffer
    long           (*stats)[STATSIZE];  // Current stats (counters for each channel and value)

    // Create new model for experiments :)
    Model (void *_buf=0, unsigned _bufsize=0) {buf=_buf, bufsize=_bufsize;};
    // Start gathering statistics for model
    void start_count (int _channels, int _bitwidth, int _offset, int _diff);
    // Count value in 0..STATSIZE range
    inline void lcount (int channel, unsigned long x);
    // Count one more, unsigned/signed value
    inline void ucount (int channel, unsigned long x);
    inline void count  (int channel, long x);
    // Calculate results of the model as level of compression using order-0 arithmetic coder
    void calc_results();
    // Run through arbitrary buffer without diffing for various bitsizes
    void _8bit_run  (void *buf, unsigned bufsize, int N);
    void _16bit_run (void *buf, unsigned bufsize, int N);
    void _24bit_run (void *buf, unsigned bufsize, int N);
    void _32bit_run (void *buf, unsigned bufsize, int N);
    void run        (void *buf, unsigned bufsize, int channels, int bitwidth);
    // Run through arbitrary buffer with diffing for various bitsizes
    void _8bit_diff_run  (void *buf, unsigned bufsize, int N);
    void _16bit_diff_run (void *buf, unsigned bufsize, int N);
    void _24bit_diff_run (void *buf, unsigned bufsize, int N);
    void _32bit_diff_run (void *buf, unsigned bufsize, int N);
    void diff_run        (void *buf, unsigned bufsize, int channels, int bitwidth);
    // Collect statistics for model with given parameters
    Model& run (int channels, int bitwidth, int offset);
    // Collect statistics for model with given parameters
    Model& diff_run (int channels, int bitwidth, int offset);
    // Collect statistics for order-0 model
    Model& run_order0 ()  {return run (1,8,0);};
    // Collect statistics for LZP/ROLZ/LZ77 model with given hash size
    Model& lzp_run (int hashlog);
    Model& rolz_run (int hashlog, int hash_row_width);
    Model& lz77_run (int hashlog, int hash_row_width);
};

// Start model
void Model::start_count (int _channels, int _bitwidth, int _offset, int _diff)
{
    // Store model's parameters
    channels = _channels;
    bitwidth = _bitwidth;
    offset   = _offset;
    diff     = _diff;
    bits     = 0;
    // Initial bit count = additinal space probably required for huffman tables (very estimated)
    xbits    = channels*128*bitwidth;
    result   = 0;
    stats    = (long (*)[STATSIZE]) calloc (channels, sizeof(*stats));
}

// Simple linear counting
inline void Model::lcount (int channel, unsigned long x)
{
    stats[channel][x]++;
}

// It's the heart of entropy metering algorithm.
// It maps usnigned value into its slot and count extra bits required to
// encode values in this slot. In order to better calculate entropy,
// each of first 256 values has its individual slot (in particular,
// this allows to precisely calculate entropy of original file using order-0
// model - such calculation may be performed by run(1,8,0) ).
// Each channel has its own set of slots, which accounts for independent encoding
// of each channel.
inline void Model::ucount (int channel, unsigned long x)
{
    if      (x < (1<<8))   stats[channel][x]++;
    else if (x < (1<<16))  stats[channel][256 + (x>>8 )]++,  xbits += 8;
    else if (x < (1<<24))  stats[channel][512 + (x>>16)]++,  xbits += 16;
    else                   stats[channel][768 + (x>>24)]++,  xbits += 24;
}

// Count for signed value (difference)
inline void Model::count (int channel, long x)
{
    ucount (channel, x>=0? x*2 : (-x)*2-1);
}

// Calculate results of the model as level of compression its output using order-0 arithmetic coder
void Model::calc_results()
{
    // Calculate number of bits required to encode values using arithmetic coder
    bits = 0;
    for (int N=0; N<channels; N++) {
        // Calculate total number of elements in this channel
        long total = 0;
        for(int i=0;i<STATSIZE;i++)  total += stats[N][i];

        for(int i=0;i<STATSIZE;i++)
            if (stats[N][i])
                bits += stats[N][i] * log(double(total/stats[N][i]))/log(double(2));
    }
    // Total result including additional bits required for extension codes
    result = (long)(bits/8 + xbits/8);
    FreeAndNil (stats);
}


// Next routines run through buffer counting statistics for elements of various size.
// They contains code specialized on N - number of channels - in order to improve performance

// Run through buffer without diffing for 8-bit elements
void Model::_8bit_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    switch (N) {
    case 1: for (unsigned char *p=(unsigned char*)buf; p+N<=bufend; p+=N)
                ucount (0, p[0]);
            break;

    case 2: for (unsigned char *p=(unsigned char*)buf; p+N<=bufend; p+=N)
                ucount (0, p[0]),
                ucount (1, p[1]);
            break;

    case 3: for (unsigned char *p=(unsigned char*)buf; p+N<=bufend; p+=N)
                ucount (0, p[0]),
                ucount (1, p[1]),
                ucount (2, p[2]);
            break;

    default:for (unsigned char *p=(unsigned char*)buf; p+N<=bufend; p+=N)
                for (int i=0; i<N; i++)
                    ucount (i, p[i]);
    }
}

// Run through buffer without diffing for 16-bit elements
void Model::_16bit_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    switch (N) {
    case 1: for (short *p=(short*)buf; p+N<=bufend; p+=N)
                count (0, p[0]);
            break;

    case 2: for (short *p=(short*)buf; p+N<=bufend; p+=N)
                count (0, p[0]),
                count (1, p[1]);
            break;

    case 3: for (short *p=(short*)buf; p+N<=bufend; p+=N)
                count (0, p[0]),
                count (1, p[1]),
                count (2, p[2]);
            break;

    default:for (short *p=(short*)buf; p+N<=bufend; p+=N)
                for (int i=0; i<N; i++)
                    count (i, p[i]);
    }
}

// Run through buffer without diffing for 24-bit elements
void Model::_24bit_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    for (char *p=(char*)buf; p+3*N<=bufend; p+=3*N)
        for (int i=0; i<N; i++)
            count (i, signed24bit(p+i));
}

// Run through buffer without diffing for 32-bit elements
void Model::_32bit_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    for (long *p=(long*)buf; p+N<=bufend; p+=N)
        for (int i=0; i<N; i++)
            count (i, p[i]);
}

// Run through buffer
void Model::run (void *buf, unsigned bufsize, int channels, int bitwidth)
{
    switch (bitwidth) {
        case  8:  _8bit_run (buf, bufsize, channels);  break;
        case 16: _16bit_run (buf, bufsize, channels);  break;
        case 24: _24bit_run (buf, bufsize, channels);  break;
        case 32: _32bit_run (buf, bufsize, channels);  break;
        default: abort();
    }
}


// Collect statistics for model with given parameters
Model& Model::run (int channels, int bitwidth, int offset)
{
    start_count (channels, bitwidth, offset, 0);
    run ((char*)buf+offset, bufsize-offset, channels, bitwidth);
    calc_results();
    return *this;
}


// Run through buffer with diffing for 8-bit elements
void Model::_8bit_diff_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    switch (N) {
    case 1: for (unsigned char *p=(unsigned char*)buf; p+N*2<=bufend; p+=N)
                ucount (0, (unsigned char) (p[1] - p[0]));
            break;

    case 2: for (unsigned char *p=(unsigned char*)buf; p+N*2<=bufend; p+=N)
                ucount (0, (unsigned char) (p[2] - p[0])),
                ucount (1, (unsigned char) (p[3] - p[1]));
            break;

    case 3: for (unsigned char *p=(unsigned char*)buf; p+N*2<=bufend; p+=N)
                ucount (0, (unsigned char) (p[3] - p[0])),
                ucount (1, (unsigned char) (p[4] - p[1])),
                ucount (2, (unsigned char) (p[5] - p[2]));
            break;

    default:for (unsigned char *p=(unsigned char*)buf; p+N*2<=bufend; p+=N)
                for (int i=0; i<N; i++)
                    ucount (i, (unsigned char) (p[N+i] - p[i]));
    }
}

// Run through buffer with diffing for 16-bit elements
void Model::_16bit_diff_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    switch (N) {
    case 1: for (short *p=(short*)buf; p+N*2<=bufend; p+=N)
                count (0, p[1] - p[0]);
            break;

    case 2: for (short *p=(short*)buf; p+N*2<=bufend; p+=N)
                count (0, p[2] - p[0]),
                count (1, p[3] - p[1]);
            break;

    case 3: for (short *p=(short*)buf; p+N*2<=bufend; p+=N)
                count (0, p[3] - p[0]),
                count (1, p[4] - p[1]),
                count (2, p[5] - p[2]);
            break;

    default:for (short *p=(short*)buf; p+N*2<=bufend; p+=N)
                for (int i=0; i<N; i++)
                    count (i, p[N+i] - p[i]);
    }
}

// Run through buffer with diffing for 24-bit elements
void Model::_24bit_diff_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    for (char *p=(char*)buf; p+3*N*2<=bufend; p+=3*N)
        for (int i=0; i<N; i++)
            count (i, (long)unsigned24bit(p+3*N+i) - (long)unsigned24bit(p+i));
}

// Run through buffer with diffing for 32-bit elements
void Model::_32bit_diff_run (void *buf, unsigned bufsize, int N)
{
    void *bufend = (char*)buf + bufsize;
    for (long *p=(long*)buf; p+N*2<=bufend; p+=N)
        for (int i=0; i<N; i++)
            count (i, p[N+i] - p[i]);
}


// Run through buffer with diffing
void Model::diff_run (void *buf, unsigned bufsize, int channels, int bitwidth)
{
    switch (bitwidth) {
        case  8:  _8bit_diff_run (buf, bufsize, channels);  break;
        case 16: _16bit_diff_run (buf, bufsize, channels);  break;
        case 24: _24bit_diff_run (buf, bufsize, channels);  break;
        case 32: _32bit_diff_run (buf, bufsize, channels);  break;
        default: abort();
    }
}


// Collect statistics for diffing model with given parameters
Model& Model::diff_run (int channels, int bitwidth, int offset)
{
    start_count (channels, bitwidth, offset, 1);
    diff_run ((char*)buf+offset, bufsize-offset, channels, bitwidth);
    calc_results();
    return *this;
}


// LZP model **************************************************************************************

enum { LZP_MATCH_FLAG=0xB5 };
static inline UINT& lzpC(BYTE* p) { return *(UINT*)(p-4); }
static inline UINT  lzpH(UINT c,BYTE* p,int HashMask) {
    return (c+11*(c >> 15)+13*lzpC(p-1)) & HashMask;
//    return (c+5*_rotr(c,17)) & HashMask;
}

// Collect statistics for LZP model with given hash size
Model& Model::lzp_run (int hashlog)
{
    start_count(2,8,0,0);   if (bufsize<32)  abort();
    BYTE* In = (BYTE*)buf;  UINT Size=bufsize;
    UINT i, k, n1=1, n=1, HashSize = 1<<hashlog, HashMask = HashSize-1;
    BYTE* p, * InEnd=In+Size, ** HTable = (BYTE**) malloc (sizeof(BYTE*) * HashSize);
    for (i=0;i < HashSize;i++)              HTable[i]=In+5;
    i=lzpC(In += 12);                       k=lzpH(i,In,HashMask);
    do {
        p=HTable[k];                        const int MinLen=1;
        if ( !--n )  { HTable[k]=In;        n=n1; }
        if (i != lzpC(p))                   ucount (0, *In++);
        else if (lzpC(p+MinLen) == lzpC(In+MinLen)) {
            for (i=4;In+i <= InEnd && lzpC(p+i) == lzpC(In+i);i += 4)
                    ;
            for (i -= 4;In+i < InEnd && In[i] == p[i];i++)
                    ;
            HTable[k]=In;                   n1 += (In-p > (n1+1)*HashSize && n1 < 7);
            ucount (0, LZP_MATCH_FLAG);     In += (k=i);
            ucount (1, i-MinLen);
            //while(int(k -= 2*n1+1) > 0)     HTable[lzpH(lzpC(In-k),In-k,HashMask)]=In-k;
        } else {
            if (*In == LZP_MATCH_FLAG)      ucount (0, 255);
            ucount (0, *In++);
        }
        k=lzpH(i=lzpC(In),In,HashMask);
    } while (In<InEnd);
    free (HTable);
    calc_results();
    return *this;
}


// ROLZ model **************************************************************************************

static inline UINT  value (BYTE* p)
{
    return value24(p-3);
}
static inline UINT  _hash (UINT c,UINT HashShift,UINT HashMask)
{
    return ((c*153191) >> HashShift) & HashMask;
}

#define hash(x)      _hash(x,HashShift,HashMask)

// Collect statistics for ROLZ model with given hash size/row width
Model& Model::rolz_run (int hashlog, int hash_row_width)
{
    start_count(2,8,0,0);   if (bufsize<32)  abort();
    BYTE* In = (BYTE*)buf;  UINT Size=bufsize;
    UINT i, k, HashSize = 1<<hashlog, HashShift = 32-hashlog,
               HashMask = (HashSize-1) & (~(hash_row_width-1));
    BYTE* p, * InEnd=In+Size-8, ** HTable = (BYTE**) malloc (sizeof(BYTE*) * HashSize);
    for (i=0;i < HashSize;i++)              HTable[i]=In+5;
    i=value(In += 12);                      k=hash(i);
    do {
        int len, best_len=0, best_j=0;                const int MinLen=1;
        BYTE* p1=In;
        for (int j=0; j<hash_row_width; j++, p1=p) {
            p=HTable[k+j];  HTable[k+j]=p1;
            if (i == value(p)  &&  value32(p+MinLen-4) == value32(In+MinLen-4)) {
                len=MinLen;
                while (In+len <= InEnd  &&  value32(p+len) == value32(In+len))  len+=4;
                while (In[len] == p[len])  len++;
                if (len>best_len)  {best_len=len; best_j=j;}
            }
        }
        if (best_len>MinLen) {
            lcount (0, 256);
            ucount (1, (best_len-MinLen)*hash_row_width + best_j);
            In += best_len;
        } else {
            lcount (0, *In++);
        }
        k=hash(i=value(In));
    } while (In<InEnd);
    free (HTable);
    calc_results();
    return *this;
}
#undef hash


// LZ77 model *************************************************************************************

// Settings for 4-byte hashing
#define value(p)     value32(p)
#define MINLEN       4

// hash function
#define hash(x)      ((((x)*153191) >> HashShift) & HashMask)

// Collect statistics for LZ77 model with given hash size
Model& Model::lz77_run (int hashlog, int hash_row_width)
{
    start_count(2,16,0,0);  if (bufsize<32)  abort();
    int literals=0, matches=0;
    void *bufend = (char*)buf + bufsize - 8;
    UINT i, HashSize = 1<<hashlog, HashShift = 32-hashlog, HashMask = ~(hash_row_width-1);
    BYTE ** HTable = (BYTE**) malloc (sizeof(BYTE*) * HashSize);
    for (i=0; i<HashSize; i++)  HTable[i]=(BYTE*)buf;

    for (BYTE *p=(BYTE*)buf+1; p<bufend; ) {
        UINT data = value(p);
        UINT h = hash(data);
        BYTE *q;
        UINT len;
switch (hash_row_width) {
case 4: {
        q = HTable[h]; BYTE *q1 = HTable[h+1], *q2 = HTable[h+2], *q3 = HTable[h+3];
        HTable[h+3] = q2, HTable[h+2] = q1, HTable[h+1] = q, HTable[h] = p;

        UINT len1;
        for (len=0; p+len<bufend && p[len]==q[len]; len++);

        for (len1=len; p+len1<bufend && p[len1]==q1[len1]; len1++);
        if (len1>len) {
            for (i=0; i<len && p[i]==q1[i]; i++);
            if (i==len) len=len1, q=q1;
        }

        for (len1=len; p+len1<bufend && p[len1]==q2[len1]; len1++);
        if (len1>len) {
            for (i=0; i<len && p[i]==q2[i]; i++);
            if (i==len) len=len1, q=q2;
        }

        for (len1=len; p+len1<bufend && p[len1]==q3[len1]; len1++);
        if (len1>len) {
            for (i=0; i<len && p[i]==q3[i]; i++);
            if (i==len) len=len1, q=q3;
        }
        break;
        }
case 2: {
        q = HTable[h]; BYTE *q1 = HTable[h+1];
        HTable[h+1] = q, HTable[h] = p;

        UINT len1;
        for (len=0; p+len<bufend && p[len]==q[len]; len++);

        for (len1=len; p+len1<bufend && p[len1]==q1[len1]; len1++);
        if (len1>len) {
            for (i=0; i<len && p[i]==q1[i]; i++);
            if (i==len) len=len1, q=q1;
        }
        break;
        }
default: // case 1
        q = HTable[h];
        HTable[h] = p;

        for (len=0; p+len<bufend && p[len]==q[len]; len++);
        break;
}

        if (len<MINLEN)  {
            stats[0][*p++]++; literals++; continue;
        }
        UINT dist = p-q;  matches++;

//            printf ("%3d %6d\n", len, dist);
        // Found match with a length 'len' and distance 'dist'
        //   Counting length in the same stats table as ordinary chars
        if      (len < (1<<8))   stats[0][256 + len]++;
        else if (len < (1<<16))  stats[0][512 + (len>>8)]++,   xbits += 8;
        else                     stats[0][768 + (len>>24)]++,  xbits += 24;
        //   Counting distance in another stats table
        int n;  for (n=0; dist>=128; n+=4,dist/=16);
        stats[0][n*32+dist]++,  xbits += n;


        // Now update 'p' and optionally insert skipped data into hashtable
#if 0
        p += len;
#else
switch (hash_row_width) {
case 1:
        h = hash(value(p+1)),  HTable[h] = p+1;
        h = hash(value(p+2)),  HTable[h] = p+2;
        p += len;
        h = hash(value(p-1)),  HTable[h] = p-1;
        break;
default:
        h = hash(value(p+1)),  HTable[h+1] = HTable[h],  HTable[h] = p+1;
        h = hash(value(p+2)),  HTable[h+1] = HTable[h],  HTable[h] = p+2;
        p += len;
        h = hash(value(p-1)),  HTable[h+1] = HTable[h],  HTable[h] = p-1;
        break;
}
#endif
    }
    free (HTable);
    printf ("   %5d %5d  ", literals, matches);
    calc_results();
    return *this;
}
#undef hash


// HIGH_LEVEL MM TYPE DETECTION ROUTINES **********************************************************

#ifndef FREEARC_DECOMPRESS_ONLY

#define WAVE_FORMAT_PCM         1
#define WAVE_FORMAT_PCM2        0xFFFE
#define WAVE_FORMAT_IEEE_FLOAT  3

#define RIFF_SIGN               0x46464952
#define WAVE_SIGN               0x45564157
#define fmt_SIGN                0x20746D66
#define data_SIGN               0x61746164

#define MAX_BPS                 32

// WAV header recognition
int autodetect_wav_header (void *buf, long size, int *is_float, int *num_chan, int *word_size, int *offset)
{
    struct wave_hdr_t {
        unsigned long   ChunkID;
        unsigned long   ChunkSize;
        unsigned long   Format;
        unsigned long   Subchunk1ID;
        unsigned long   Subchunk1Size;
        unsigned short  AudioFormat;
        unsigned short  NumChannels;
        unsigned long   SampleRate;
        unsigned long   ByteRate;
        unsigned short  BlockAlign;
        unsigned short  BitsPerSample;
    } *wave_hdr = (wave_hdr_t*)buf;

    // check for supported formats
    if (size                    < sizeof(*wave_hdr)   ||
        wave_hdr->ChunkID       != RIFF_SIGN          ||
        wave_hdr->Format        != WAVE_SIGN          ||
        wave_hdr->Subchunk1ID   != fmt_SIGN           ||
        wave_hdr->Subchunk1Size > wave_hdr->ChunkSize ||
        wave_hdr->NumChannels   == 0                  ||
        wave_hdr->BitsPerSample > MAX_BPS)
            return 0;  // detection fails

    switch (wave_hdr->AudioFormat) {
    case WAVE_FORMAT_IEEE_FLOAT:   *is_float = 1; break;
    case WAVE_FORMAT_PCM:
    case WAVE_FORMAT_PCM2:         *is_float = 0; break;
    default:                       return 0;  // detection fails
    }

    // p will point after all headers, just to the samples beginning
    BYTE *p = (BYTE*)buf + sizeof(*wave_hdr);

    // Skip extra header bytes
    if (wave_hdr->Subchunk1Size > 16) {
         if (wave_hdr->Subchunk1Size-16 >= (BYTE*)buf+size-p)   return 0;  // detection fails
         p += wave_hdr->Subchunk1Size-16;
    }

    // Skip any extra subchunks and header of wave data subchunk itself
    struct subchunk_hdr {
        unsigned long   SubchunkID;
        unsigned long   SubchunkSize;
    };

    if (sizeof(subchunk_hdr) >= (BYTE*)buf+size-p)   return 0;  // detection fails

    while (((subchunk_hdr*)p)->SubchunkID != data_SIGN) {
        ulong skip_bytes  =  sizeof(subchunk_hdr) + ((subchunk_hdr*)p)->SubchunkSize;
        if (skip_bytes+sizeof(subchunk_hdr) >= (BYTE*)buf+size-p)   return 0;  // detection fails
        p += skip_bytes;
    }
    p += sizeof(subchunk_hdr);

    *num_chan  = wave_hdr->NumChannels;
    *word_size = wave_hdr->BitsPerSample;
    *offset    = p - (BYTE*)buf;
    return 1;  // detection OK
}


// ****************************************************************************************
// ** Analyze data and return number of channels/wordsize/offset we should use.
// ** channels[]/bitvalues[] are the variants of number of channels/wordsizes we should try
// ****************************************************************************************
int autodetect_by_entropy (void *buf, int bufsize, int channels[], int bitvalues[], double min_entropy, int *is_float, int *num_chan, int *word_size, int *offset)
{
    if (bufsize<500)  return 0;    // Not enough data for detection
    Model model (buf, bufsize);    // Object for testing various models of MM compression

    // Collect stats for order-0 model for comparison
    model.run_order0();
    double model0_result = model.result;
    //printf("order0: entropy %.2f, min %.2f\n", double(model.result)/bufsize, min_entropy);
    // If simple order-0 model can make file much smaller then
    // this file probably isn't good MM one
    if (model0_result < bufsize*min_entropy) {
        return 0;  // MM data not found
    }

    // Best model so far
    Model best_model;
    best_model.result = LONG_MAX;

    // Collect stats for various number of channels and bitwidth
    for (int i=0; ; i++) {
        int N=channels[i];  if (N==0)  break;
        for (int j=0; ; j++) {
            int bits=bitvalues[j];  if (bits==0)  break;
            for (int offset=0; offset*8<bits; offset++) {
                // Try MM with given parameters and estimate compressed size
                model.diff_run (N, bits, offset);
                //printf("%d*%d+%d: entropy %.2f", N, bits, offset, double(model.result)/bufsize);
                // Select best model so far
                if (model.result < best_model.result
                    && model.bitwidth >= best_model.bitwidth)    best_model = model/*, printf(" - better!")*/;
                // Also, prefer new model if it uses larger words and still "good enough"
                if (model.bitwidth > best_model.bitwidth
                    &&  model.result < best_model.result*1.05
                    &&  model.result < model0_result*0.95)       best_model = model/*, printf(" - good enough ;)")*/;
                // Opposite case - prefer new model if it has less bits but improves compression at least 5%
                if (model.result < best_model.result*0.95)    best_model = model/*, printf(" - much better!")*/;
                //printf("\n");
            }
        }
    }

    // Use MM compression if it provides at least 5% better compression compared to order-0 model
    if (best_model.result < model0_result*0.95) {
        // Use parameters of best model detected
        *is_float  = 0;
        *num_chan  = best_model.channels;
        *word_size = best_model.bitwidth;
        *offset    = best_model.offset;
        return 1;  // detection OK
    } else {
        return 0;  // detection fails
    }
}


// ****************************************************************************************
// ** Analyze data to check for multimedia
// ****************************************************************************************

// Number of channels that multimedia file may have. Used for auto-detection when
// header of any known file type doesn't exist or don't taken into account. This array is zero-ended
static int channels[] = {1,2,3,4,0};
// Number of bits per word that multimedia file may have. Also used for auto-detection
static int bitvalues[] = {8,16,24,32,0};

// Values used in fast detection mode
static int fast_channels[] = {1,3,0}, fast_bitvalues[] = {8,16,0};

// How many bytes to check, depending on speed mode and filesize
int detect_mm_bytes (int mode, int filesize)
{
  return mymin (mode<=2? 64*kb : mymax(64*kb,mymin(filesize,1*mb)/2), filesize);
}

// Check whether buffer contains MM data
int detect_mm (int mode, void *buf, int bufsize)
{
  int is_float, num_chan, word_size, offset;
  int *use_channels  = mode<=2? fast_channels  : channels;
  int *use_bitvalues = mode<=2? fast_bitvalues : bitvalues;
  return autodetect_by_entropy (buf, bufsize, use_channels, use_bitvalues, 0.80, &is_float, &num_chan, &word_size, &offset);
}

// Check whether buffer contains header of MM file
int detect_mm_header (int mode, void *buf, int bufsize)
{
  int is_float, num_chan, word_size, offset;
  return autodetect_wav_header (buf, bufsize, &is_float, &num_chan, &word_size, &offset);
}


// ****************************************************************************************
// ** Analyze data and return best guess of their type (binary, text, compressed...)
// ****************************************************************************************
int count_desc_order (const int *a, const int *b)   { return *b-*a; }

void detect_datatype (BYTE *buf, int bufsize, char *type)
{
    // Вычисляем распределение частот символов и одновременно..
    int count[256];  iterate(256, count[i]=0);
    // ..ищем матчи с помощью простой LZ-схемы и определяем долю REPDIST кодов среди них
    int matches=0, repdists=0, sumlen=0, dist[4]={0,0,0,0};
#define TABLE_SIZE 16384
#define hash(p)  (value16(p) % TABLE_SIZE)
    BYTE **table = (BYTE**) calloc(TABLE_SIZE, sizeof(BYTE*));
    if (bufsize>10) for (BYTE *p=buf; p<buf+bufsize-10; p++) {
       BYTE *q = table[hash(p)];
       if (q && value16(p)==value16(q)) {
           if (p-q<256) {  // собираем статистику только по матчам с дистанцией <256
               matches++;
               int x=p-q;
               for (int i=0; i<elements(dist); i++) {
                   int y=dist[i];  dist[i]=x;  x=y;
                   if (x==p-q)  {repdists++; break;}
               }
           }
           // Пропускаем найденный матч, не забывая обновлять счётчик/поисковый хеш
           BYTE *o=p;
           while (value32(p)==value32(q)  &&  p<buf+bufsize-10)
              count[p[0]]++, table[hash(p+0)]=p,
              count[p[1]]++, table[hash(p+1)]=p+1,
              count[p[2]]++, table[hash(p+2)]=p+2,
              count[p[3]]++, table[hash(p+3)]=p+3,
              p+=4, q+=4;
           while (*p==*q  &&  p<buf+bufsize-10)  count[*p]++, table[hash(p)]=p, p++, q++;  count[*p]++;
           if (o-q<256)  sumlen += p-o;
       } else {
           count[*p]++,  table[hash(p)] = p;    // матч не найден
       }

    }
    free(table);
#undef TABLE_SIZE
#undef hash

    // Сжатие order-0 моделью
    double order0=0;
    for (int i=0; i<256; i++)
    {
        if (count[i])
            order0 += count[i] * log(double(bufsize/count[i]))/log(double(2)) / 8;
    }

    // Кол-во символов, занимающих 90% объёма текста
    int sums=0, total=bufsize, normal_chars=256;
    qsort (count, 256, sizeof(*count),
           (int (*)(const void*, const void*)) count_desc_order);
    for (int i=0; i<256; i++)
    {
        if (count[i] > bufsize*0.20)  {total-=count[i]; continue;}
        sums += count[i];
        if (sums > total*0.90)
        {
            normal_chars = i;
            break;
        }
    }

    // Тип данных: $compressed если не сжимается ни order-0 ни lz77.
    //             $text если активных символов от 17 до 80, число повторов дистанций невелико и lz-матчи составляют хотя бы 10% данных
    strcpy (type, buf==NULL                                 ? "$compressed $text" :  // list of data types this procedure is able to recognize

                  order0 > 0.95*bufsize
                    // && matches < bufsize/100
                    && sumlen < 0.10*bufsize                ? "$compressed" :

                  17<=normal_chars && normal_chars<=80
                    // && matches < bufsize/6
                    && repdists < 0.20*matches
                    && sumlen   > 0.10*bufsize              ? "$text" :
                                                              "default");
    //if(!bufsize) printf ("\n");
    //if(bufsize)  printf (" %8s %5d %5.2lf%% ", type, bufsize, textlike*100);
    //if(normal)   printf ("    %5.0lf %5.0lf %5.2lf%% %5.2lf \n", double(normal), double(used_chars), rare*100, q);
    //if(bufsize) printf (" %8s %d\n", type, normal_chars);
    //if(bufsize)  printf (" %8s %5.2lf%% %5d %5.2lf%% %5.0lf %5.2lf %5.2lf%%\n", type, order0/bufsize*100, normal_chars, double(repdists+1)/(matches+1)*100, double(matches), double(sumlen)/(matches+1), double(sumlen)/(bufsize+1)*100);
    //(printf("\n\n\nSTAT: normal_chars=%d, repdists=%d, matches=%d, sumlen=%d, bufsize=%d\n\n %*.*s\n", normal_chars, repdists, matches, sumlen, bufsize, bufsize, bufsize, buf), "default"));
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)



#ifndef MMD_LIBRARY
// DRIVER ************************************************************************
// This demo program shows how to use MM detector and moreover
// demonstrates more complex technique of gathering statistics
// while processing file in small chunks

// Вычисление времени работы алгоритма
#include <io.h>
#include <windows.h>
static LARGE_INTEGER Frequency, PerformanceCountStart, PerformanceCountEnd;
void init_timer (void)
{
    QueryPerformanceFrequency (&Frequency);
    QueryPerformanceCounter (&PerformanceCountStart);
}

double timer (void)
{
    QueryPerformanceCounter (&PerformanceCountEnd);
    double res = double(PerformanceCountEnd.QuadPart - PerformanceCountStart.QuadPart)/Frequency.QuadPart;
    PerformanceCountStart = PerformanceCountEnd;
    return res;
}

unsigned filesize;

void print_stats (char *model_name, Model model)
{
    printf ("%-36s:%9ld +%9ld =%9ld bytes (%3.1lf%%): %.3lf seconds\n", model_name,
             (long)(model.bits/8), model.xbits/8, model.result, (double)model.result*100/filesize, timer());
}

int main (int argc, char **argv)
{
    // LZP/ROLZ/LZ77 compression modes testing
    int lz_mode = 0;

    // LZ compression modes testing
    int lz77_mode = 0;

    // Filetype detection mode
    int det_mode = 0;


    if (nameequ(argv[1], "-lz")) {
        argv++; argc--; lz_mode++;
    }
    if (nameequ(argv[1], "-lz77")) {
        argv++; argc--; lz77_mode++;
    }
    if (nameequ(argv[1], "-det")) {
        argv++; argc--; det_mode++;
    }
    if (argc!=2) {
        printf( "\n Usage: mmdet [-lz/-lz77/-det] file");
        exit(1);
    }
    FILE *fin = fopen( argv[1], "rb" );
    if (fin == NULL) {
        printf( "\n Can't open %s for read\n", argv[1]);
        exit(2);
    }

    filesize = filelength(fileno(fin));
    char *buf = (char*) malloc(filesize);
    if (buf == NULL) {
        printf( "\n Can't alloc %u bytes\n", filesize);
        exit(4);
    }

    unsigned bytes = fread (buf, 1, filesize, fin);
    if (bytes != filesize) {
        printf( "\n Can't read entire input file");
        exit(5);
    }
    printf( "File size: %u\n", filesize);
    init_timer();


    // Maximum number of channels we are trying to use
    const int MAX_CHANNELS = 6;

    // Name and result of best model so far
    char best_model_name[100];
    long best_result = LONG_MAX;

    Model lz (buf, filesize);
    char title[100] = "";

if (!lz_mode && !lz77_mode && !det_mode) {
#if 1
    Model model (buf, filesize);

    // Calc stats for various number of channels, bitwidth and enabled/disabled diff'ing
    for (int diff=0; diff<=1; diff++) {
        for (int N=1; N<=MAX_CHANNELS; N++) {
            for (int bits=8; bits<=32; bits+=8) {
                    for (int offset=0; offset*8<bits; offset++) {
                    // Prepare name for this model
                    char channelsStr[100], offsetStr[100], model_name[100];
                    sprintf (channelsStr, N>1?    " * %d channels" : "", N);
                    sprintf (offsetStr,   bits>8? ", offset %d"    : "", offset);
                    sprintf (model_name, "%2dbit%s%s%s", bits, channelsStr, offsetStr, diff? ", diffed" : "");

                    // Try model with given parameters, collect appropriate statistics and print it
                    diff?  model.diff_run (N, bits, offset)
                        :  model.run (N, bits, offset);
                    print_stats (model_name, model);

                    // Select best model so far
                    if (model.result < best_result) {
                        best_result = model.result;
                        strcpy (best_model_name, model_name);
                    }
                }
            }
            printf ("\n");
        }
    }

    // Collect stats for order-0 model for comparison
    Model order0 (buf, filesize);
    print_stats ("Order-0",  order0.run_order0());
#else
// ****************************************************************************
// ** EXAMPLE OF PROCESSING DATA IN SMALL CHUNKS ******************************
// ****************************************************************************
    // Room to store all models we check here
    Model models[2][MAX_CHANNELS+1][32+1];

    // Initialize counters for all models used
    for (int diff=0; diff<=1; diff++)
        for (int N=1; N<=MAX_CHANNELS; N++)
            for (int bits=8; bits<=32; bits+=8)
                models[diff][N][bits].start_count (N, bits, 0, diff);

    // Run through buffer in small chunks and update statistics
    // for all models on each chunk
    int CHUNK = 12<<12;
    for (int n=0; n+CHUNK<=filesize; n+=CHUNK)
        for (int N=1; N<=MAX_CHANNELS; N++)
            for (int bits=8; bits<=32; bits+=8) {
                models[0][N][bits].run (buf+n, CHUNK, N, bits);
                models[1][N][bits].diff_run (buf+n, CHUNK, N, bits);
            }

    // Print results for all models and select the best one
    for (int diff=0; diff<=1; diff++) {
        for (int N=1; N<=MAX_CHANNELS; N++) {
            for (int bits=8; bits<=32; bits+=8) {
                // Prepare name for this model
                char channels[100], model_name[100];
                sprintf (channels, N>1? " * %d channels" : "", N);
                sprintf (model_name, "%2dbit%s%s", bits, channels, diff? ", diffed" : "");

                // Calculate and print model results
                Model& model = models[diff][N][bits];
                model.calc_results();
                print_stats (model_name, model);

                // Select best model so far
                if (model.result < best_result) {
                    best_result = model.result;
                    strcpy (best_model_name, model_name);
                }
            }
            printf ("\n");
        }
    }
    Model order0 = models[0][1][8];
#endif

    // Run LZP/ROLZ/LZ77 models for comparison
    Model lz (buf, filesize);
    //print_stats ("LZP, 16kb hash",  lz.lzp_run  (12));
    //print_stats ("ROLZ, 16kb hash", lz.rolz_run (12,1));
    print_stats ("LZ77, 16kb hash", lz.lz77_run (12,1));

    // Print WAV file header parameters if such header present
    int is_float, num_chan, word_size, offset;
    if (autodetect_wav_header (buf, bytes, &is_float, &num_chan, &word_size, &offset))
        printf ("WAV header: %dbit%s * %d channel(s), offset %d\n", word_size, is_float? " float":"", num_chan, offset);

    // Use MM compression if it provides at least 5% better compression than order-0 model
    if (best_result >= order0.result)
        printf ("MM compression? ha-ha, forget about this!   ");
    else
        printf ("BEST model is %s (%3.1f%% compared to order-0 model)   ",
                best_model_name, (double)best_result*100/order0.result);
}

else if (lz_mode)   // Run LZP/ROLZ/LZ77 models for comparison
{
    for (int h=12; h<=22; h++, h>18 && h++) {
        sprintf(title, "LZP, %5dkb hash", 1<<(h-8));
        print_stats (title, lz.lzp_run (h));
    }
    printf ("\n");
    for (int h=12; h<=22; h++, h>18 && h++) {
        sprintf(title, "ROLZ, %5dkb hash%d", 1<<(h-8),4);
        print_stats (title, lz.rolz_run (h,4));
    }
    printf ("\n");
    for (int h=12; h<=22; h++, h>18 && h++) {
        sprintf(title, "LZ77, %5dkb hash%d", 1<<(h-8),4);
        print_stats (title, lz.lz77_run (h,4));
    }
}
else if (lz77_mode)   // Try LZ77 model with various hashsize and hashchain length
{
    for (int l=1; l<=4; l*=2) {
        if (*title)  printf ("\n");
        for (int h=12; h<=22; h++, h>18 && h++) {
            sprintf(title, "LZ77, %5dkb hash%d", 1<<(h-8),l);
            print_stats (title, lz.lz77_run (h,l));
        }
    }
}
else if (det_mode)   // Binary/text/compressed detection mode
{
    init_timer();
    char result[100];
    for (int i=0; i+100<filesize; i+=65536)
        detect_datatype ((BYTE*)buf+i, mymin(65536,filesize-i), result);
    //detect_datatype ((BYTE*)buf, filesize, result);
    //printf("%.3lf sec", timer());
}
    return 0;
}

#endif

