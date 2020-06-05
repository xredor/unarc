// CLS version 0.10
#define CLS_API_VERSION 10

#include <stdlib.h>
#include <string.h>

// All CLS structures are packed in order to avoid compiler incompatibilities
#pragma pack(push, CLS, 1)

// Type of callback passed to ClsMain (see below)
//   instance           - identifies this particular ClsMain call
//   callback_operation - callback operation requested (CLS_FULL_READ, CLS_GET_PARAMSTR and so on)
//   ptr, n             - generic parameters, whose interpretation depends on particular operation. If more parameters are required, ptr points to the record
//   returns negative error code on failure and for some operations informative positive value
typedef int __cdecl CLS_CALLBACK (void* instance, int callback_operation, void *ptr, int n);

// Type of ClsMain. It's the one and only function that implements all the codec functionality
//   operation - operation requested (CLS_COMPRESS and so on)
//   callback  - function that compressor calls to receive any functionality from the caller (including I/O and compression parameters)
//   instance  - passed to callback to distinguish this particular call
//   returns negative error code on failure and for some operations informative positive value
typedef int __cdecl CLS_MAIN (int operation, CLS_CALLBACK callback, void* instance);

// Function that implements whole codec functionality
extern "C" CLS_MAIN ClsMain;

// Operations
const int CLS_INIT                          = 1;       // Called on codec load in order to get more info about it
const int CLS_DONE                          = 2;       // Called before code unload
const int CLS_FLUSH                         = 6;       // Called after batch of operations were performed in order to allow codec to release resources
const int CLS_COMPRESS                      = 3;       // Request to compress (encode) data using I/O via callbacks
const int CLS_DECOMPRESS                    = 4;       // Request to decompress (decode) data using I/O via callbacks
const int CLS_PREPARE_METHOD                = 5;       // Apply all restrictions to method and then return its new formatted params and info (memreqs/blocksize/threads...)

// Callback operations
const int CLS_FULL_READ                     = 4096;    // Read data into buffer ptr:n. Use CLS_FULL_READ+i to read i'th stream. Retcode: <0 - error, 0 - EOF, >0 - amount of data read
const int CLS_PARTIAL_READ                  = 5120;    // Read data partially (buffer may be not filled entirely even if EOF isn't reached). CLS_PARTIAL_READ+i also works. Retcode: the same
const int CLS_FULL_WRITE                    = 6144;    // Write data from buffer ptr:n. CLS_FULL_WRITE+i also works. Retcode: the same
const int CLS_PARTIAL_WRITE                 = 7168;    // Write data partially (number of bytes written may be less than n)
const int CLS_MALLOC                        = 1;       // Alloc n bytes and make *ptr point to this area
const int CLS_FREE                          = 2;       // Free ptr previously allocated by CLS_MALLOC
const int CLS_GET_PARAMSTR                  = 3;       // Put ASCIIZ parameters string into buffer ptr:n
const int CLS_SET_PARAMSTR                  = 4;       // Set parameter string to ASCIIZ string pointed by ptr
const int CLS_THREADS                       = 5;       // How many threads*100?
const int CLS_MEMORY                        = 6;       // How much memory?
const int CLS_DECOMPRESSION_MEMORY          = 7;       // How much memory for decompression?
const int CLS_DECOMPRESSOR_VERSION          = 8;       // Codec version required for decompression
const int CLS_BLOCK                         = 9;       // Block size (for block-wise compressors like bwt)
const int CLS_EXPAND_DATA                   = 10;      // May this compressor expand data (like precomp)?
// INIT-stage callback operations
const int CLS_ID                            = 101;     // Codec ID as unique ASCIIZ string (for example, "lzma.7zip.org")
const int CLS_VERSION                       = 102;     // Codec version as ASCIIZ string (for example, "1.2.3")
const int CLS_THREAD_SAFE                   = 103;     // Is the DLL thread-safe, i.e. allows to run multiple copies of ClsMain() simultaneously?

// Error codes
const int CLS_OK                            = 0;       // ALL RIGHT
const int CLS_ERROR_GENERAL                 = -1;      // Unclassified error
const int CLS_ERROR_NOT_IMPLEMENTED         = -2;      // Requested feature isn't supported
const int CLS_ERROR_NOT_ENOUGH_MEMORY       = -3;      // Memory allocation failed
const int CLS_ERROR_READ                    = -4;
const int CLS_ERROR_WRITE                   = -5;
const int CLS_ERROR_ONLY_DECOMPRESS         = -6;      // This DLL supports only decompression
const int CLS_ERROR_INVALID_COMPRESSOR      = -7;      // Invalid compression method parameters
const int CLS_ERROR_BAD_COMPRESSED_DATA     = -8;      // Data can't be decompressed
const int CLS_ERROR_NO_MORE_DATA_REQUIRED   = -9;      // Required part of data was already decompressed
const int CLS_ERROR_OUTBLOCK_TOO_SMALL      = -10;     // Output block size in (de)compressMem is not enough for all output data

// Method parameter types
const int CLS_PARAM_INT                     = -1;      // Integer parameter, like grzip:m1
const int CLS_PARAM_STRING                  = -2;      // String  parameter, like lzma:mf=bt4
const int CLS_PARAM_MEMORY_MB               = -3;      // Memory  parameter, like lzma:d64m, measured in megabytes by default

// Various sizes
const int CLS_MAX_PARAMSTR_SIZE             = 256;     // Maximum size of string returned by CLS_GET_PARAMSTR
const int CLS_MAX_ERROR_MSG                 = 256;     // Maximum size of error message

// Macro that simplifies verifying operation success
#define CHECK_CLS_OK(action)  {int cls_retcode = (action);  if (cls_retcode!=CLS_OK)  return cls_retcode;}

/* to do:
- also there're threading issues (like, application allowing to use up to N
threads) and whether dll is thread-safe or not (if not, it can be secured
by loading multiple instances of dll - might be a useful feature as many
experimental compressors are not really incapsulated).
- codecs need to know how much memory for compression / decompression they are supposed to use.
- some codecs might handle multithreading in a smarter way than splitting streams.

- check versions and backward/forward compatibility
- allow multiple codecs in same dll. this may be solved by ClsMain2, ClsMain3... exported
but this may be not enough for some more complex scenarios
+ some interface methods are required for initialization and model flush
(which are not the same as there might be some precalculation required
only once).
- what about detectors
+ now read may be partial, while write should be full. how about full read / partial write?
- alternative i/o: request outbuf, point to the next inbuf

method:, compression:, and 5-6 options of their choice that would show in GUI
*/


// C++ interface simplifying codec development

#ifdef __cplusplus

// Exceptions used to return from CLS operation with error code and message
class ClsException
{
public:
    int  code;
    char msg[CLS_MAX_ERROR_MSG];
    ClsException(int errcode)  {code = errcode; strcpy(msg, "");}
};

// Structure describing one method parameter
struct ParamParser
{
    char *name;                    // option name, say "m"
    void *ptr;                     // pointer to the place where parsed value should be placed
    int  type;                     // option type: integer, string, memory..
    bool (*validator)(void* ptr);  // validator for value parsed
};

// Base class that simplify development of compressors in C++
class ClsCompressor
{
protected:
    // Saved parameters passed to ClsMain
    int operation; CLS_CALLBACK callback; void* instance;

    // Descendant-overridden functions
    virtual void load()       {throw ClsException(CLS_ERROR_NOT_IMPLEMENTED);}  // Called just once on DLL load
    virtual void unload()     {throw ClsException(CLS_ERROR_NOT_IMPLEMENTED);}  // Called just once on DLL unload
    virtual void compress()   {throw ClsException(CLS_ERROR_NOT_IMPLEMENTED);}  // Request to compress data using read/write interface
    virtual void decompress() {throw ClsException(CLS_ERROR_NOT_IMPLEMENTED);}  // Request to decompress data using read/write interface

    virtual void parse_params(char *params)  {}
    virtual void fill_params(char *params)   {}
    virtual ParamParser *get_parser()        {return NULL;}
    virtual void apply_constraints()         {}


    // Read/write data.
    // Partial read may fill only part of buffer. It's useful for stream-style processing.
    // Full read fills entire buffer, except reading at EOF which may return less data. Useful for blockwise processing.
    // Exact read generates exception if it can't fill entire buffer.
    // Read operations return 0 only on EOF.
    // Full write generates exception if it can't flush entire buffer. Useful for most cases.
    // Partial write may flush only part of buffer. It may be faster if you are ready to deal with it.
    virtual int  full_read    (void *ptr, int n)  {return checked_callback(CLS_FULL_READ,     ptr, n);}
    virtual int  partial_read (void *ptr, int n)  {return checked_callback(CLS_PARTIAL_READ,  ptr, n);}
    virtual void full_write   (void *ptr, int n)  {       checked_callback(CLS_FULL_WRITE,    ptr, n);}
    virtual int  partial_write(void *ptr, int n)  {return checked_callback(CLS_PARTIAL_WRITE, ptr, n);}

    // Throw exception if requested amount of bytes can't be read. Useful for reading fixed-size fields
    virtual void read_exactly (void *ptr, int n)
    {
        int result = full_read(ptr, n);
        if (result != n)
            throw ClsException(CLS_ERROR_READ);
    }


    // Perform checked callback and raise exception if operation fails
    virtual int checked_callback(int callback_operation, void *ptr, int n)
    {
        int result = callback(instance, callback_operation, ptr, n);
        if (result < 0)
            throw ClsException(result);
        return result;
    }

    // Perform checked callback and raise exception if operation fails (except for case it's not implemented)
    virtual int optional_callback(int callback_operation, void *ptr, int n)
    {
        int result = callback(instance, callback_operation, ptr, n);
        if (result < 0  &&  result != CLS_ERROR_NOT_IMPLEMENTED)
            throw ClsException(result);
        return result;
    }

public:
    // Main function that executes host's request
    virtual int run()
    {
        try
        {
            switch(operation)
            {
            case CLS_INIT:       load(); break;
            case CLS_DONE:       unload(); break;

            case CLS_COMPRESS:
            case CLS_DECOMPRESS:
            case CLS_PREPARE_METHOD:
                {
                    char params[CLS_MAX_PARAMSTR_SIZE] = "";     // Default value for the case CLS_GET_PARAMSTR not implemented in host
                    optional_callback(CLS_GET_PARAMSTR, params, CLS_MAX_PARAMSTR_SIZE);
                    parse_params(params);
                    apply_constraints();
                    switch(operation)
                    {
                    case CLS_COMPRESS:        compress(); break;
                    case CLS_DECOMPRESS:      decompress(); break;

                    case CLS_PREPARE_METHOD:
                        fill_params(params);  // Keep original params if fill_params() isn't implemented in descendant
                        optional_callback(CLS_SET_PARAMSTR, params, CLS_MAX_PARAMSTR_SIZE);
                        break;
                    }
                }

            default:
                return CLS_ERROR_NOT_IMPLEMENTED;
            }
        }
        catch (ClsException e)
        {
            return e.code;
        }
        return CLS_OK;
    }
};

/* How to implement ClsMain using this class:

class MyCompressor : public ClsCompressor
{
    virtual void compress()   {...}    // read() -> process -> write() -> ...
    virtual void decompress() {...}
optionally:
    virtual ParamParser *get_parser() {...}
    virtual void apply_constraints()  {...}
    ...
};

extern "C" int __cdecl ClsMain(int operation, CLS_CALLBACK callback, void* instance)
{
    return MyCompressor(operation, callback, instance).run();
}
*/

#endif


#pragma pack(pop, CLS)

