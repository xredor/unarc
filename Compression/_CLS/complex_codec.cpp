#include "cls.h"

class Compressor : public ClsCompressor
{
    int memlevel;       // 1..9
    int speed;          // 1..9
    virtual void compress();
    virtual void decompress();
};

// Get method params as a string and parse them
void Compressor::parse_params (char *params)
{
    ParamParser p[] = { {"m", &mem,   CLS_PARAM_MEMORY_MB, NULL}
                      , {"o", &order, CLS_PARAM_INT      , NULL}
                      , {"e", &speed, CLS_PARAM_STRING   , NULL}
                      };
    cls_parse(p, params);
    if (not_in (speed, "0 s f n x xx"))
        throw CLS_EXCEPTION(CLS_ERROR_INVALID_COMPRESSOR, "Invalid -e.. parameter");
}

// Send measurements of algorithm to host, get and apply constraints and send final measurements back
void Compressor::apply_constraints()
{
    CLS_PARAMS5 p = {1, 1<<(memlevel+22), 1<<(memlevel+18), 0, 0};
    cls_set(&p); cls_get(&p);                                   // pass alg. params to host and get constraints
    if (op != CLS_DECOMPRESS)                                   // satisfy constraints
    {
        if (p.cmem && p.dmem)
            memlevel = min (logb(p.cmem)-22, logb(p.dmem)-18);
        else if (p.cmem)
            memlevel = logb(p.cmem)-22;
        else if (p.dmem)
            memlevel = logb(p.dmem)-18;
    }
    CLS_PARAMS5 pf = {1, 1<<(memlevel+22), 1<<(memlevel+18), 0, 0};
    cls_set(&pf);                                               // pass final params to host
}

// Perform actual compression
void Compressor::compress()
{
    const int BUFSIZE = 4096;  // 1<<(memlevel+22);
    char buf[BUFSIZE];
    for (int len; (len = partial_read(buf, BUFSIZE)) != 0; )
    {
        full_write(buf, len);
    }
}

// Perform actual decompression
void Compressor::decompress()
{
    const int BUFSIZE = 4096;  // 1<<(memlevel+18);
    char buf[BUFSIZE];
    for (int len; (len = partial_read(buf, BUFSIZE)) != 0; )
    {
        full_write(buf, len);
    }
}

// Main DLL function
int ClsMain (int op, CLS_CALLBACK callback, void* instance)
{
    Compressor(op, callback, instance).run();
}
