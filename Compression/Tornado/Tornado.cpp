// (c) Bulat Ziganshin <Bulat.Ziganshin@gmail.com>
// (c) Joachim Henke
// GPL'ed code of Tornado - fast LZ77 compression algorithm.
#include "../Compression.h"
#include "MatchFinder.cpp"
#include "EntropyCoder.cpp"
#include "LZ77_Coder.cpp"
#include "DataTables.cpp"

// Compression method parameters
struct PackMethod
{
    int  number;            // Preset number
    int  encoding_method;   // Coder (0 - storing, 1 - bytecoder, 2 - bitcoder, 3 - huffman, 4 - arithmetic)
    bool find_tables;       // Enable searching for MM tables
    int  hash_row_width;    // Length of hash row
    uint hashsize;          // Hash size
    int  caching_finder;    // Force/prohibit using caching match finder
    uint buffer;            // Buffer (dictionary) size
    int  match_parser;      // Match parser (1 - greedy, 2 - lazy, 3 - flexible, 4 - optimal, 5 - even better)
    int  hash3;             // 2/3-byte hash presence and type
    int  shift;             // How much bytes to shift out/keep when window slides
    int  update_step;       // How much bytes are skipped in mf.update()
    uint auxhash_size;      // Auxiliary hash size
    int  auxhash_row_width; // Length of auxiliary hash row
};

extern "C" {
// Main compression and decompression routines
int tor_compress   (PackMethod m, CALLBACK_FUNC *callback, void *auxdata);
int tor_decompress (CALLBACK_FUNC *callback, void *auxdata);
}

enum { STORING=0, BYTECODER=1, BITCODER=2, HUFCODER=3, ARICODER=4 };
enum { GREEDY=1, LAZY=2 };

// Preconfigured compression modes
PackMethod std_Tornado_method[] =
    //                 tables row  hashsize  caching buffer parser  hash3 shift update   auxhash
    { {  0, STORING,   false,   0,        0, 0,      1*mb,  0     ,   0,    0,  999,       0,    0 }
    , {  1, BYTECODER, false,   1,    16*kb, 0,      1*mb,  GREEDY,   0,    0,  999,       0,    0 }
    , {  2, BITCODER,  false,   1,    64*kb, 0,      2*mb,  GREEDY,   0,    0,  999,       0,    0 }
    , {  3, HUFCODER,  true,    2,   128*kb, 0,      4*mb,  GREEDY,   0,    0,  999,       0,    0 }
    , {  4, HUFCODER,  true,    2,     2*mb, 1,      8*mb,  GREEDY,   0,    0,  999,       0,    0 }
    , {  5, ARICODER,  true,    4,     2*mb, 1,     16*mb,  LAZY  ,   1,    0,  999,       0,    0 }
    , {  6, ARICODER,  true,    8,    32*mb, 1,     64*mb,  LAZY  ,   1,    0,    4,       0,    0 }
    , {  7, ARICODER,  true,   32,   128*mb, 5,    256*mb,  LAZY  ,   2,    0,    1,  128*kb,    4 }
    , {  8, ARICODER,  true,  128,   512*mb, 5,   1024*mb,  LAZY  ,   2,    0,    1,  128*kb,    4 }
    , {  9, ARICODER,  true,  256,  2048*mb, 5,   1024*mb,  LAZY  ,   2,    0,    1,  512*kb,    4 }
    , { 10, ARICODER,  true,  256,  2048*mb, 6,   1024*mb,  LAZY  ,   2,    0,    1,    2*mb,   32 }
    , { 11, ARICODER,  true,  200,  1600*mb, 7,   1024*mb,  LAZY  ,   2,    0,    1,  512*mb,  256 }
    };

// Default compression parameters are equivalent to option -5
const int default_Tornado_method = 5;

// If data table was not encountered in last table_dist bytes, don't check next table_shift bytes in order to make things faster
const int table_dist=256*1024, table_shift=128;

// Minimum lookahead for next match which compressor tries to guarantee.
// Also minimum amount of allocated space after end of buf (this allows to use things like p[11] without additional checks)
#define LOOKAHEAD 256

// Output buffer size
uint tornado_compressor_outbuf_size (uint buffer, int bytes_to_compress = -1)
{return bytes_to_compress!=-1? bytes_to_compress+(bytes_to_compress/8)+512 :
        compress_all_at_once?  buffer+(buffer/8)+512 :
                               HUGE_BUFFER_SIZE;}


#ifndef FREEARC_DECOMPRESS_ONLY

// Check for data table with N byte elements at current pos
#define CHECK_FOR_DATA_TABLE(N)                                                                         \
{                                                                                                       \
    if (p[-1]==p[N-1]                                                                                   \
    &&  uint(p[  N-1] - p[2*N-1] + 4) <= 2*4                                                            \
    &&  uint(p[2*N-1] - p[3*N-1] + 4) <= 2*4                                                            \
    &&  !val32equ(p+2*N-4, p+N-4))                                                                      \
    {                                                                                                   \
        int type, items;                                                                                \
        if (check_for_data_table (N, type, items, p, bufend, table_end, buf, offset, last_checked)) {   \
            coder.encode_table (type, items);                                                           \
            /* If data table was diffed, we should invalidate match cached by lazy match finder */      \
            mf.invalidate_match();                                                                      \
            goto found;                                                                                 \
        }                                                                                               \
    }                                                                                                   \
}


// Read next datachunk into buffer, shifting old contents if required
template <class MatchFinder, class Coder>
int read_next_chunk (PackMethod &m, CALLBACK_FUNC *callback, void *auxdata, MatchFinder &mf, Coder &coder, byte *&p, byte *buf, BYTE *&bufend, BYTE *&table_end, BYTE *&last_found, BYTE *&read_point, int &bytes, int &chunk, uint64 &offset, byte *(&last_checked)[MAX_TABLE_ROW][MAX_TABLE_ROW])
{
    if (bytes==0 || compress_all_at_once)  return 0;     // All input data was successfully compressed
    // If we can't provide 256 byte lookahead then shift data toward buffer beginning,
    // freeing space at buffer end for the new data
    if (bufend-buf > m.buffer-LOOKAHEAD) {
        int sh;
        if (m.shift==-1) {
            sh = p-(buf+2);  // p should become buf+2 after this shift
            memcpy (buf, buf+sh, bufend-(buf+sh));
            mf.clear_hash (buf);
        } else {
            sh = m.shift>0? m.shift : bufend-buf+m.shift;
            memcpy (buf, buf+sh, bufend-(buf+sh));
            mf.shift (buf, sh);
        }
        p      -= sh;
        bufend -= sh;
        offset += sh;
        if (coder.support_tables && m.find_tables)
            table_end  = table_end >buf+sh? table_end -sh : buf,
            last_found = last_found>buf+sh? last_found-sh : buf;
        iterate_var(i,MAX_TABLE_ROW)  iterate_var(j,MAX_TABLE_ROW)  last_checked[i][j] = buf;
        mf.invalidate_match();  // invalidate match stored in lazy MF; otherwise it may fuck up the NEXT REPCHAR checking
        coder.shift_occurs();   // Tell to the coder what shift occurs
        debug (printf ("==== SHIFT %08x: p=%08x ====\n", sh, p-buf));
    }
    bytes = callback ("read", bufend, mymin (chunk, buf+m.buffer-bufend), auxdata);
    debug (printf ("==== read %08x ====\n", bytes));
    if (bytes<0)  return bytes;    // Return errcode on error
    bufend += bytes;
    read_point = bytes==0? bufend:bufend-LOOKAHEAD;
    coder.flush();          // Sometimes data should be written to disk :)
    return p<bufend? 1 : 0; // Result code: 1 if we still have bytes to compress, 0 otherwise
}


// Compress one chunk of data
template <class MatchFinder, class Coder>
int tor_compress_chunk (PackMethod m, CALLBACK_FUNC *callback, void *auxdata, byte *buf, int bytes_to_compress)
{
    // Read data in these chunks
    int chunk = compress_all_at_once? m.buffer : mymin (m.shift>0? m.shift:m.buffer, LARGE_BUFFER_SIZE);
    uint64 offset = 0;                        // Current offset of buf[] contents relative to file (increased with each shift() operation)
    int bytes = bytes_to_compress!=-1? bytes_to_compress : callback ("read", buf, chunk, auxdata);   // Number of bytes read by last "read" call
    if (bytes<0)  return bytes;               // Return errcode on error
    BYTE *bufend = buf + bytes;               // Current end of real data in buf[]
    BYTE *matchend = bufend - mymin (MAX_HASHED_BYTES, bufend-buf);   // Maximum pos where match may finish (less than bufend in order to simplify hash updating)
    BYTE *read_point = compress_all_at_once || bytes_to_compress!=-1? bufend : bufend-mymin(LOOKAHEAD,bytes); // Next point where next chunk of data should be read to buf
    // Match finder will search strings similar to current one in previous data
    MatchFinder mf (buf, m.hashsize, m.hash_row_width, m.auxhash_size, m.auxhash_row_width);
    if (mf.error() != FREEARC_OK)  return mf.error();
    // Coder will encode LZ output into bits and put them to outstream
    Coder coder (m.encoding_method, callback, auxdata, tornado_compressor_outbuf_size (m.buffer, bytes_to_compress), chunk*2);   // Data should be written in HUGE_BUFFER_SIZE chunks (at least) plus chunk*2 bytes should be allocated to ensure that no buffer overflow may occur (because we flush() data only after processing each 'chunk' input bytes)
    if (coder.error() != FREEARC_OK)  return coder.error();
    BYTE *table_end  = coder.support_tables && m.find_tables? buf : buf+m.buffer+LOOKAHEAD;    // The end of last data table processed
    BYTE *last_found = buf;                             // Last position where data table was found
    byte *last_checked[MAX_TABLE_ROW][MAX_TABLE_ROW];   // Last position where data table of size %1 with offset %2 was tried
    if(coder.support_tables)  {iterate_var(i,MAX_TABLE_ROW)  iterate_var(j,MAX_TABLE_ROW)  last_checked[i][j] = buf;}
    // Use first output bytes to store encoding_method, minlen and buffer size
    coder.put8 (m.encoding_method);
    coder.put8 (mf.min_length());
    coder.put32(m.buffer);
    // Encode first four bytes directly (at least 2 bytes should be saved directly in order to avoid problems with using p-2 in MatchFinder.update())
    for (BYTE *p=buf; p<buf+4; p++) {
        if (p>=bufend)  goto finished;
        coder.encode (0, p, buf, mf.min_length());
    }

    // ========================================================================
    // MAIN CYCLE: FIND AND ENCODE MATCHES UNTIL DATA END
    for (BYTE *p=buf+4; TRUE; ) {
        // Read next chunk of data if all data up to read_point was already processed
        if (p >= read_point) {
            if (bytes_to_compress!=-1)  goto finished;  // We shouldn't read/write any data!
            byte *p1=p;  // This trick allows to not take address of p and this buys us a bit better program optimization
            int res = read_next_chunk (m, callback, auxdata, mf, coder, p1, buf, bufend, table_end, last_found, read_point, bytes, chunk, offset, last_checked);
            p=p1, matchend = bufend - mymin (MAX_HASHED_BYTES, bufend-buf);
            if (res==0)  goto finished;    // All input data was successfully compressed
            if (res<0)   return res;       // Error occured while reading data
        }

        // Check for data table that may be subtracted to improve compression
        if (coder.support_tables  &&  p > table_end) {
            if (mf.min_length() < 4)                      // increase speed by skipping this check in faster modes
              CHECK_FOR_DATA_TABLE (2);
            CHECK_FOR_DATA_TABLE (4);
            if (p-last_found > table_dist)  table_end = p + table_shift;
            goto not_found;
            found: last_found=table_end;
            not_found:;
        }

        // Find match length and position
        UINT len = mf.find_matchlen (p, matchend, 0);
        BYTE *q  = mf.get_matchptr();
        // Encode either match or literal
        if (!coder.encode (len, p, q, mf.min_length())) {      // literal encoded
            print_literal (p-buf+offset, *p); p++;
        } else {                                               // match encoded
            // Update hash and skip matched data
            check_match (p, q, len);
            print_match (p-buf+offset, len, p-q);
            mf.update_hash (p, len, m.update_step);
            p += len;
        }
    }
    // END OF MAIN CYCLE
    // ========================================================================

finished:
    stat (printf("\nTables %d * %d = %d bytes\n", int(table_count), int(table_sumlen/mymax(table_count,1)), int(table_sumlen)));
    // Return mf/coder error code or mark data end and flush coder
    if (mf.error()    != FREEARC_OK)   return mf.error();
    if (coder.error() != FREEARC_OK)   return coder.error();
    coder.encode (IMPOSSIBLE_LEN, buf, buf-IMPOSSIBLE_DIST, mf.min_length());
    coder.finish();
    return coder.error();
}


// tor_compress template parameterized by MatchFinder and Coder
template <class MatchFinder, class Coder>
int tor_compress0 (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
    //SET_JMP_POINT( FREEARC_ERRCODE_GENERAL);
    // Make buffer at least 32kb long and round its size up to 4kb chunk
    m.buffer = (mymax(m.buffer, 32*kb) + 4095) & ~4095;
    // If hash is too large - make it smaller
    if (m.hashsize/8     > m.buffer)  m.hashsize     = 1<<lb(m.buffer*8);
    if (m.auxhash_size/8 > m.buffer)  m.auxhash_size = 1<<lb(m.buffer*8);
    // >0: shift data in these chunks, <0: how many old bytes should be kept when buf shifts,
    // -1: don't slide buffer, fill it with new data instead
    m.shift = m.shift?  m.shift  :  (m.hash_row_width>4? m.buffer/4   :
                                     m.hash_row_width>2? m.buffer/2   :
                                     m.hashsize>=512*kb? m.buffer/4*3 :
                                                         -1);
    // Alocate buffer for input data
    byte *buf = (byte*) calloc (m.buffer+LOOKAHEAD, 1);     // make Valgrind happy :)
    if (!buf)  return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;

#if 0
    // Create compression threads
    for (int i=0; i<NumThreads; i++)
    {
        CThread t; t.Create(TornadoCompressionThread, &job[i]);
    }
    // Perform I/O and assign compression jobs
    for (int i=0; ; i=(i+1)%NumThreads)
    {
        // Save results of previous compression job
        job[i].Finished.Wait();
        callback ("write", job[i].outbuf, job[i].outsize, auxdata);

        // Read next chunk of data
        int bytes = callback ("read", buf, m.buffer, auxdata);   // Number of bytes read by last "read" call
        if (bytes<0)  return bytes;               // Return errcode on error

        // Send signal to start compression
        job[i].Compress.Signal();
    }
#endif

    // MAIN COMPRESSION FUNCTION
    int result = tor_compress_chunk<MatchFinder,Coder> (m, callback, auxdata, buf, -1);

    free(buf);
    return result;
}


template <class MatchFinder, class Coder>
int tor_compress4 (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
    switch (m.match_parser) {
    case GREEDY: return tor_compress0 <             MatchFinder,  Coder> (m, callback, auxdata);
    case LAZY:   return tor_compress0 <LazyMatching<MatchFinder>, Coder> (m, callback, auxdata);
    }
}

template <class MatchFinder, class Coder>
int tor_compress3 (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
    switch (m.hash3) {
    case 0: return tor_compress4 <MatchFinder, Coder> (m, callback, auxdata);
    case 1: return tor_compress4 <Hash3<MatchFinder,12,10,FALSE>, Coder> (m, callback, auxdata);
    case 2: return tor_compress4 <Hash3<MatchFinder,16,12,TRUE >, Coder> (m, callback, auxdata);
    }
}

template <class MatchFinder>
int tor_compress2 (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
    switch (m.encoding_method) {
    case STORING:   // Storing - go to any tor_compress2 call
    case BYTECODER: // Byte-aligned encoding
                    return tor_compress3 <MatchFinder, LZ77_ByteCoder>                          (m, callback, auxdata);
    case BITCODER:  // Bit-precise encoding
                    return tor_compress3 <MatchFinder, LZ77_BitCoder>                           (m, callback, auxdata);
    case HUFCODER:  // Huffman encoding
                    return tor_compress3 <MatchFinder, LZ77_Coder <HuffmanEncoder<EOB_CODE> > > (m, callback, auxdata);
    case ARICODER:  // Arithmetic encoding
                    return tor_compress3 <MatchFinder, LZ77_Coder <ArithCoder<EOB_CODE> >     > (m, callback, auxdata);
    }
}

template <class MatchFinder>
int tor_compress2d (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
    return tor_compress3 <MatchFinder, LZ77_DynamicCoder> (m, callback, auxdata);
}

// Compress data using compression method m and callback for i/o
int tor_compress (PackMethod m, CALLBACK_FUNC *callback, void *auxdata)
{
// When FULL_COMPILE is defined, we compile all the 4*8*3*2=192 possible compressor variants
// Otherwise, we compile only 8 variants actually used by -0..-11 predefined modes
#ifdef FULL_COMPILE
    switch (m.caching_finder) {
    case 7:  if (m.hash_row_width > 256)  return FREEARC_ERRCODE_INVALID_COMPRESSOR;
             return tor_compress2d <CombineMF <CycledCachingMatchFinder<7>, CycledCachingMatchFinder<4> > > (m, callback, auxdata);
    case 6:  if (m.hash_row_width > 256)  return FREEARC_ERRCODE_INVALID_COMPRESSOR;
             return tor_compress2d <CombineMF <CycledCachingMatchFinder<6>, CycledCachingMatchFinder<4> > > (m, callback, auxdata);
    case 5:  if (m.hash_row_width > 256)  return FREEARC_ERRCODE_INVALID_COMPRESSOR;
             return tor_compress2d <CombineMF <CycledCachingMatchFinder<5>, ExactMatchFinder<4> > >         (m, callback, auxdata);
    case 2:  if (m.hash_row_width > 256)  return FREEARC_ERRCODE_INVALID_COMPRESSOR;
             return tor_compress2d <CycledCachingMatchFinder<4> > (m, callback, auxdata);
    case 1:  return tor_compress2  <CachingMatchFinder<4> >       (m, callback, auxdata);

    default: switch (m.hash_row_width) {
             case 1:    return tor_compress2 <MatchFinder1>     (m, callback, auxdata);
             case 2:    return tor_compress2 <MatchFinder2>     (m, callback, auxdata);
             default:   return tor_compress2 <MatchFinderN<4> > (m, callback, auxdata);
             }
    }
#else
    // -1..-5(-6)
    if (m.encoding_method==BYTECODER && m.hash_row_width==1 && m.hash3==0 && !m.caching_finder && m.match_parser==GREEDY ||
        m.encoding_method==STORING ) {
        return tor_compress0 <MatchFinder1, LZ77_ByteCoder> (m, callback, auxdata);
    } else if (m.encoding_method==BITCODER && m.hash_row_width==1 && m.hash3==0 && !m.caching_finder && m.match_parser==GREEDY ) {
        return tor_compress0 <MatchFinder1, LZ77_BitCoder > (m, callback, auxdata);
    } else if (m.encoding_method==HUFCODER && m.hash_row_width==2 && m.hash3==0 && !m.caching_finder && m.match_parser==GREEDY ) {
        return tor_compress0 <MatchFinder2, LZ77_Coder<HuffmanEncoder<EOB_CODE> > > (m, callback, auxdata);
    } else if (m.encoding_method==HUFCODER && m.hash_row_width>=2 && m.hash3==0 && m.caching_finder && m.match_parser==GREEDY ) {
        return tor_compress0 <CachingMatchFinder<4>, LZ77_Coder<HuffmanEncoder<EOB_CODE> > > (m, callback, auxdata);
    } else if (m.encoding_method==ARICODER && m.hash_row_width>=2 && m.hash3==1 && m.caching_finder==1 && m.match_parser==LAZY ) {
        return tor_compress0 <LazyMatching<Hash3<CachingMatchFinder<4>,12,10,FALSE> >, LZ77_Coder<ArithCoder<EOB_CODE> > > (m, callback, auxdata);
    // -5 -c3 - used for FreeArc -m4$compressed
    } else if (m.encoding_method==HUFCODER && m.hash_row_width>=2 && m.hash3==1 && m.caching_finder==1 && m.match_parser==LAZY ) {
        return tor_compress0 <LazyMatching<Hash3<CachingMatchFinder<4>,12,10,FALSE> >, LZ77_Coder<HuffmanEncoder<EOB_CODE> > > (m, callback, auxdata);

    // -7..-9
    } else if (m.hash_row_width>=2 && m.hash3==2 && m.caching_finder==5 && m.match_parser==LAZY ) {
        return tor_compress0 <LazyMatching <CombineMF <CycledCachingMatchFinder<5>, Hash3<ExactMatchFinder<4>,16,12,TRUE> > >,
                              LZ77_DynamicCoder > (m, callback, auxdata);
    // -10 and -11
    } else if (m.hash_row_width>=2 && m.hash3==2 && m.caching_finder==6 && m.match_parser==LAZY ) {
        return tor_compress0 <LazyMatching <CombineMF <CycledCachingMatchFinder<6>, Hash3<CycledCachingMatchFinder<4>,16,12,TRUE> > >,
                              LZ77_DynamicCoder > (m, callback, auxdata);
    } else if (m.hash_row_width>=2 && m.hash3==2 && m.caching_finder==7 && m.match_parser==LAZY ) {
        return tor_compress0 <LazyMatching <CombineMF <CycledCachingMatchFinder<7>, Hash3<CycledCachingMatchFinder<4>,16,12,TRUE> > >,
                              LZ77_DynamicCoder > (m, callback, auxdata);

    } else {
        return FREEARC_ERRCODE_INVALID_COMPRESSOR;
    }
#endif
}

#endif // FREEARC_DECOMPRESS_ONLY

// LZ77 decompressor ******************************************************************************

// If condition is true, write data to outstream
#define WRITE_DATA_IF(condition)                                                                  \
{                                                                                                 \
    if (condition) {                                                                              \
        if (decoder.error() != FREEARC_OK)  goto finished;                                        \
        tables.undiff_tables (write_start, output);                                               \
        debug (printf ("==== write %08x:%x ====\n", write_start-outbuf+offset, output-write_start)); \
        WRITE (write_start, output-write_start);                                                  \
        tables.diff_tables (write_start, output);                                                 \
        write_start = output;  /* next time we should start writing from this pos */              \
                                                                                                  \
        /* Check that we should shift the output pointer to start of buffer */                    \
        if (output >= outbuf + bufsize) {                                                         \
            offset_overflow |= (offset > (uint64(1) << 63));                                      \
            offset      += output-outbuf;                                                         \
            write_start -= output-outbuf;                                                         \
            write_end   -= output-outbuf;                                                         \
            tables.shift (output,outbuf);                                                         \
            output      -= output-outbuf;  /* output = outbuf; */                                 \
        }                                                                                         \
                                                                                                  \
        /* If we wrote data because write_end was reached (not because */                         \
        /* table list was filled), then set write_end into its next position */                   \
        if (write_start >= write_end) {                                                           \
            /* Set up next write chunk to HUGE_BUFFER_SIZE or until buffer end - whatever is smaller */ \
            write_end = write_start + mymin (outbuf+bufsize-write_start, HUGE_BUFFER_SIZE);       \
        }                                                                                         \
    }                                                                                             \
}


template <class Decoder>
int tor_decompress0 (CALLBACK_FUNC *callback, void *auxdata, int _bufsize, int minlen)
{
    //SET_JMP_POINT (FREEARC_ERRCODE_GENERAL);
    int errcode = FREEARC_OK;                             // Error code of last "write" call
    Decoder decoder (callback, auxdata, _bufsize);        // LZ77 decoder parses raw input bitstream and returns literals&matches
    if (decoder.error() != FREEARC_OK)  return decoder.error();
    uint bufsize = compress_all_at_once? _bufsize : mymax (_bufsize, HUGE_BUFFER_SIZE);   // Make sure that outbuf is at least 8mb in order to avoid excessive disk seeks (not required in programs compiled for one-shot compression)
    BYTE *outbuf = (byte*) BigAlloc (bufsize+PAD_FOR_TABLES*2);  // Circular buffer for decompressed data
    if (!outbuf)  return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
    outbuf += PAD_FOR_TABLES;       // We need at least PAD_FOR_TABLES bytes available before and after outbuf in order to simplify datatables undiffing
    BYTE *output      = outbuf;     // Current position in decompressed data buffer
    BYTE *write_start = outbuf;     // Data up to this point was already writen to outsream
    BYTE *write_end   = outbuf + mymin (bufsize, HUGE_BUFFER_SIZE); // Flush buffer when output pointer reaches this point
    if (compress_all_at_once)  write_end = outbuf + bufsize + 1;    // All data should be written after decompression finished
    uint64 offset = 0;                    // Current outfile position corresponding to beginning of outbuf
    int offset_overflow = 0;              // Flags that offset was overflowed so we can't use it for match checking
    DataTables tables;                    // Info about data tables that should be undiffed
    for (;;) {
        // Check whether next input element is a literal or a match
        if (decoder.is_literal()) {
            // Decode it as a literal
            BYTE c = decoder.getchar();
            print_literal (output-outbuf+offset, c);
            *output++ = c;
            WRITE_DATA_IF (output >= write_end);  // Write next data chunk to outstream if required

        } else {
            // Decode it as a match
            UINT len  = decoder.getlen(minlen);
            UINT dist = decoder.getdist();
            print_match (output-outbuf+offset, len, dist);

            // Check for simple match (i.e. match not requiring any special handling, >99% of matches fail to this category)
            if (output-outbuf>=dist && write_end-output>len) {
                BYTE *p = output-dist;
                do   *output++ = *p++;
                while (--len);

            // Check that it's a proper match
            } else if (len<IMPOSSIBLE_LEN) {
                // Check that compressed data are not broken
                if (dist>bufsize || len>2*_bufsize || (output-outbuf+offset<dist && !offset_overflow))  {errcode=FREEARC_ERRCODE_BAD_COMPRESSED_DATA; goto finished;}
                // Slow match copying route for cases when output-dist points before buffer beginning,
                // or p may wrap at buffer end, or output pointer may run over write point
                BYTE *p  =  output-outbuf>=dist? output-dist : output-dist+bufsize;
                do {
                    *output++ = *p++;
                    if (p==outbuf+bufsize)  p=outbuf;
                    WRITE_DATA_IF (output >= write_end);
                } while (--len);

            // Check for special len/dist code used to encode EOF
            } else if (len==IMPOSSIBLE_LEN && dist==IMPOSSIBLE_DIST) {
                WRITE_DATA_IF (TRUE);  // Flush outbuf
                goto finished;

            // Otherwise it's a special code used to represent info about diffed data tables
            } else {
                len -= IMPOSSIBLE_LEN;
                if (len==0 || dist*len > 2*_bufsize)  {errcode=FREEARC_ERRCODE_BAD_COMPRESSED_DATA; goto finished;}
                stat (printf ("\n%d: Start %x, end %x, length %d      ", len, int(output-outbuf+offset), int(output-outbuf+offset+len*dist), len*dist));
                // Add new table to list: len is row length of table and dist is number of rows
                tables.add (len, output, dist);
                // If list of data tables is full then flush it by preprocessing
                // and writing to outstream already filled part of outbuf
                WRITE_DATA_IF (tables.filled());
            }
        }
    }
finished:
    BigFree(outbuf-PAD_FOR_TABLES);
    // Return decoder error code, errcode or FREEARC_OK
    return decoder.error() < 0 ?  decoder.error() :
           errcode         < 0 ?  errcode
                               :  FREEARC_OK;
}


int tor_decompress (CALLBACK_FUNC *callback, void *auxdata)
{
    int errcode;
    // First 6 bytes of compressed data are encoding method, minimum match length and buffer size
    BYTE buf[2];          READ (buf, 2);
    uint encoding_method; encoding_method = buf[0];
    uint minlen;          minlen          = buf[1];
    uint bufsize;         READ4 (bufsize);

    switch (encoding_method) {
    case BYTECODER:
            return tor_decompress0 <LZ77_ByteDecoder> (callback, auxdata, bufsize, minlen);

    case BITCODER:
            return tor_decompress0 <LZ77_BitDecoder>  (callback, auxdata, bufsize, minlen);

    case HUFCODER:
            return tor_decompress0 <LZ77_Decoder <HuffmanDecoder<EOB_CODE> > > (callback, auxdata, bufsize, minlen);

    case ARICODER:
            return tor_decompress0 <LZ77_Decoder <ArithDecoder<EOB_CODE> >   > (callback, auxdata, bufsize, minlen);
    default:
            errcode = FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
    }
finished: return errcode;
}


/*
LZ77 model:
    -no lz if len small and dist large: don't have much sense with our MINLEN=4
    -hash4+3: only 1% gain even on ghc.exe
    -hash5+4: 48->46.7 mb but 2x slower (22->46sec: 240mb compressed using 16mb hash)
    -0x65a8e9b4 for hash
    +combined len+dist encoding a-la cabarc - will make decoding a bit faster, but who cares? :)
    -save into hash records unused part of hash value in order to make
        fast check of usability of this hash slot (like it is already
        done in REP); would be especially helpful on larger hashes
    -save into hash record 4 bits of p[5] - would be useful to skip trying second..fourth hash records
    +save into hash record 4 bytes of data
    +lazy search (and return of 3-byte strings) for highest compression mode
+l8... - добавило 1 лишнюю секунду на обработку каждых 280 мб
+compare with ideal hash function crc+crc+..
    (((CRCTab[(x)&255] ^ _rotr(CRCTab[((x)>>8)&255],8) ^ _rotr(CRCTab[((x)>>16)&255],16) ^ _rotr(CRCTab[((x)>>24)&255],24)) >> HashShift) & HashMask)
+store unused hash bits + a few more chars in hash   (1.5x speedup)
    491->367 (340 for hash4x), 91->68, 51->43 secs
    +использовать первый байт под хеш 4х байтов
    +отдельные циклы для len=3,4,5,6
    +используя t, быстро проверять матчи длины до 7 в циклах len3..5 и при проверке первой строки
    проверить заново длины совпадений строк в хеш-цепочке
+fast arithmetics! total=2^n
    отдельный буфер для чтения битовых полей; или лучше bits+arith в одном потоке данных
+lazy matches                                        (+3.5% compression)
    unsuccessfully tried:
      ush good_length; - reduce lazy search above this match length
      ush max_lazy;    - do not perform lazy search above this match length
      ush nice_length; - quit search above this match length
+arith / huffman / bitio                         (+10% compresion for bit i/o, +20% for huffman)
    byte i/o -> class: +0.3 sec on !all
+3-byte strings
+выкидывать короткие далёкие строки
    +можно улучшить сжатие на 0.3% если выкидывать ещё и 6-байтовые строки
+better hash multiplier
-5% less compression of src (l4 h22) compared to mmdet. strange?
-several encoding tables: after char, after small string, large string
-add custom MF for l=4/8 (3/6?) what means -1 sec. on !all
    don't have much meaning because caching MF isn't any worser
+FIXED: MatchFinder2 несовместим с 3-байтовыми словами / lazy matching (update_hash рассчитано на обновления как минимум в 3 байта)
+FAST_COMPILE - only 4 models actually used by -1..-12
+сделать hash_row_width частью класса MatchFinder
+FIXED: caching MF - нечётные слова должны инициализироваться содержимым начала буфера
+sliding window for higher modes (-4/-5 - m.buffer/2, -6 and up - m.buffer/4)
+write data to outstreams in 16mb chunks
+64k-1m non-sliding window for -1..-3
+improved caching MF - memory accesses only for matches>=7 or last check
-max_lazy may improve speed/ratio for -4..-6 modes
-don't check more than one real string (option? only for 2-element hash?)
    -skip checking second string if first is large enough
+[almost] full hash_update for highest modes
+IMPOSSIBLE_LEN/IMPOSSIBLE_DIST for EOF encoding, encode() for first 2 chars
+FIXED: -s- -p2 problem (was returning len==0 instead of MINLEN-1)
-при lazy поиске учитывать длину пред. матча, пропуская 3-байтовый и часть 4-байтового поиска
+TOO_FAR checks moved into caching MF
+output buffer now flushed only when reading next input chunk
+tor_(de)compress - returns error code or FREEARC_OK
+freearc: блокировать тред чтения при записи данных
+7z's lazy heuristic
  +при поиске строки - if newlen=len+1 and newdist>dist*64 - ignore it
+2-byte strings, +repdist, +repboth, +repchar
+обработка маленьких файлов!
+восстановить bytecoder
  +large len - a few bytes representation to ensure no overflows
+auto-decrease hash (and buf) for small files
+удлинять назад next match в lazy matcher
-repdistN+-delta - 0.4% на текстах
+HuffmanEncoder::encode2
+fixed: использование в проверке на REPCHAR инициализационного значения repdist0=1
        использование псевдодистанции от MMx для проверки на REPCHAR (учти: декодер должен иметь ту же очередь последних дистанций)
        переход diffed table через сдвиг буфера
          восстановление данных должно делаться после обратного diff, иначе этот diff запишет мусор в элемент, следующий за восстановленным
        использование p->table_len вместо обрезанного len
        write_end мог выходить за границу буфера
        read_next_chunk должен возвращать 0 если больше сжимать нечего (последний матч добил до конца уже прочитанных данных и новых прочесть не удалось)
        101..104 не совсем аккуратно использовался для data table codes
-context-based char encoding
  separate coder table after \0 or after \0..\31
+diffing tables
-repboth, repchar1..3
-split caching hash into two parts - pointers and data
  +cyclic hash for large N
+ChangePair in MFN
  -ChangePair for len1-len2>1
при достаточно длинном и далёком матче выкидывать его из хеша в предположении, что текущая строка его прекрасно заменит
  -делать сдвиг отдельно, после цикла поиска матчей (попробовано при неразделённом CMF)
block-static arithmetic coder - may improve compression by 1-2%
? caching MF для -l2
+ 5/6-byte main hash for highest modes (-7 and up)
hash3+lazy - скомбинировать в другом порядке, поскольку нет смысла искать 3-байтовую строку после матча?
заполнить конец буфера случайными данными и убрать проверки p+len<bufend
  заменить проверки p+len<=bufend одной в compress0()
ограничить проверяемую дистанцию в -1/-2/-3? чтобы не вылезать за размер кеша
rolz 1+2+3+4
minor thoughts:
  small outbuf for -5 and higher modes
  increase HUFBLOCKSIZE for -2/-3  (100k - -0.2sec)

text files -5/-6: disable 2/3-byte searching, repchar and use encode(..., MINLEN=4), switch to hufcoder(?)
hufcoder: disable REPDIST, +fast qsort<>
huf&ari: EOB, check for text-like stats, switch into text mode

use only one bit for flag in bytecoder
bitcoder: 30-bit length encoding - make it a part of 8-bit encoding
huf/ari - improve "first block" encoding, adaptation (currently, up to 1/64 of codespace is wasted),
  +EOB code
? выводить данные блоками, соответствующими входным chunks, storing несжавшихся блоков
    header = 1 byte flags + 3 bytes len
более детализированные disttables для маленьких len
-1,-2,-3?: +no MM, no REP*
huf/ari: вместо cnt++ делать cnt+=10 - должно увеличить точность кодирования (это увеличивает размер таблиц, что замедляет кодирование; возмсожно, проблему можно решить использованием 3-уровневых таблиц кодирования)
ST4/BWT sorting for exhaustive string searching

ускорение tor:5
  -ускорение lazy поиска (Кадач)
  ускорение сравнения матчей (идея автора QuickLZ)
  -искать MM tables by rep* codes
  оптимизировать huf и перейти на него
  для текстов:
    не использовать 2/3-byte matches
    использовать huf c большим блоком вместо арифметики
    не проверять на repchar/repdist/repboth
    не искать MM tables

ускорение/улучшение сжатия tor:7-12
  +использовать бессдвиговую технологию хеширования и -u1
  +2/3hash: увеличить размер, вставлять все строки
  +искать в большом хеше строки длины >=6/7, спихнув меньшие во вспомогат. хэш
  пропускать символы 0/' ' при хешировании
  check matches at repdist distances


+-h1mb in cmdline
+-z/-d options, by default auto depending on file extension
+-h1m -9 == -9 -h1m (учитывать сначала выбор пресета, затем уточняющие его опции)
+-odir/ -odir\ -od:
+64-bit insize/outsize
+-b128k, m.hashsize вместо hashlog, print block/hashsize in help with k/m suffix
+CHECK mallocs
+dir_exists=file_exists(dir\.) || end_with(:/\)
+progress indicator in console title
-t, -f force overwrite, -k keep src files, stdin->stdout by default
make non-inline as much functions as possible (optimize .exe size): +MatchFinder.cpp +LZ77_Coder.cpp
****Tornado 0.2 compressing VC, 41243 kb     --noheader option disables this
****-1: 16kb hash1...: done 5%
****-1: 16kb hash1...: 17876 kb (12.7%), 23.333 sec, 88.6 mb/s
.tor signature, version, flags, crc
? записывать сжатые данные перед чтением следующего chunk и использовать storing при отсутствии сжатия (обнулять huf/ari-table)
? уменьшить хеш назад вдвое (сначала проверить эффект на других файлах, 200-300 kb на all)
+print predefined methods definitions in help screen
-mem должно демонстрировать режимы сжатия от -1 до -9?  -bench для моих внутренних тестов
tor_compress: при сжатии файла ==buffer происходит лишний перенос данных перед тем, как прочесть 0 байт :)

Changes in 0.2:
    lazy parsing
    3-byte matches
    huffman coder
    sliding window

Changes in 0.3:
    repdist&repchar0 codes
    2-byte matches
    optimized lz parsing
    table preprocessing
    gzip-like cmdline interface?

    -1 thor e1, quicklz
    -2 thor e2, slug
    -3 thor e3, gzip -1
    -4 gzip, rar -m1
    -5 thor, 7zip -mx1
    -6 uharc -mz
    -7 bzip2, rar -m2

Changes in 0.4:
    Cyclic caching MF (makes -9..-11 modes faster)
    Full 2/3-byte hashing in -9..-11 modes which improved compression a bit
    Improved console output to provide more information

*/
