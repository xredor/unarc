// C_LZMA.cpp - интерфейс FreeArc к алгоритму сжатия LZMA

#ifdef WIN32
#include <windows.h>
#include <initguid.h>
#else
#define INITGUID
#endif

// Enable multithreading support
#define COMPRESS_MF_MT

// Match finder classes
#define MF_HashChain  0
#define MF_BinaryTree 1
#define MF_HashTable  2

extern "C" {
#include "C_LZMA.h"
}

enum
{
  kBT2,
  kBT3,
  kBT4,
  kHC4,
  kHT4
};

static const char *kMatchFinderIDs[] =
{
  "BT2",
  "BT3",
  "BT4",
  "HC4",
  "HT4"
};

static int FindMatchFinder(const char *s)
{
  for (int m = 0; m < (int)(sizeof(kMatchFinderIDs) / sizeof(kMatchFinderIDs[0])); m++)
    if (!strcasecmp(kMatchFinderIDs[m], s))
      return m;
  return -1;
}

// Включим в один .o файл все необходимые подпрограммы
#include "C/LzmaDec.c"
#undef kNumFullDistances
#ifndef FREEARC_DECOMPRESS_ONLY
namespace LzmaEncoder {
#include "C/LzFind.c"
#include "C/LzmaEnc.c"
#undef SKIP_HEADER
#undef SKIP_FOOTER
#ifdef COMPRESS_MF_MT
#include "C/Threads.c"
#include "C/LzFindMt.c"
#endif
}
using namespace LzmaEncoder;
#endif

static void *SzAlloc(void *p, size_t size) { p = p; return BigAlloc(size); }
static void SzFree(void *p, void *address) { p = p; BigFree(address); }
static ISzAlloc g_Alloc = { SzAlloc, SzFree };

int SRes_to_FreeArc (SRes res)
{
  if (res == SZ_ERROR_DATA)         return FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
  if (res == SZ_ERROR_MEM)          return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
  if (res == SZ_ERROR_UNSUPPORTED)  return FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
  if (res == SZ_ERROR_PARAM)        return FREEARC_ERRCODE_INVALID_COMPRESSOR;
  if (res == SZ_ERROR_INPUT_EOF)    return FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
  if (res == SZ_ERROR_OUTPUT_EOF)   return FREEARC_ERRCODE_WRITE;
  if (res == SZ_ERROR_READ)         return FREEARC_ERRCODE_READ;
  if (res == SZ_ERROR_WRITE)        return FREEARC_ERRCODE_WRITE;
  if (res != SZ_OK)
    //fprintf(stderr, "\nEncoder error = %X\n", (unsigned int)res);
    return FREEARC_ERRCODE_GENERAL;
  return FREEARC_OK;
}

SRes LzmaDec_AllocateUsingProperties(CLzmaDec *p, const CLzmaProps propNew, ISzAlloc *alloc)
{
  SizeT dicBufSize;
  RINOK(LzmaDec_AllocateProbs2(p, &propNew, alloc));
  dicBufSize = propNew.dicSize;
  if (p->dic == 0 || dicBufSize != p->dicBufSize)
  {
    LzmaDec_FreeDict(p, alloc);
    p->dic = (Byte *)alloc->Alloc(alloc, dicBufSize);
    if (p->dic == 0)
    {
      LzmaDec_FreeProbs(p, alloc);
      return SZ_ERROR_MEM;
    }
  }
  p->dicBufSize = dicBufSize;
  p->prop = propNew;
  return SZ_OK;
}

// Input buffer size for decompressor
uint RangeDecoderBufferSize (uint dict)
{return compress_all_at_once? dict : LARGE_BUFFER_SIZE;}     //// dict*1.1?




#ifndef FREEARC_DECOMPRESS_ONLY

typedef struct
{
  SRes (*Read)(void *p, void *buf, size_t *size);
    /* if (input(*size) != 0 && output(*size) == 0) means end_of_stream.
       (output(*size) < input(*size)) is allowed */
  CALLBACK_FUNC *callback;
  void *auxdata;
  bool first_read;
  int errcode;
} CallbackInStream;

typedef struct
{
  size_t (*Write)(void *p, const void *buf, size_t size);
    /* Returns: result - the number of actually written bytes.
       (result < size) means error */
  CALLBACK_FUNC *callback;
  void *auxdata;
  int errcode;
} CallbackOutStream;


SRes CallbackRead(void *p, void *buf, size_t *size)
{
  CallbackInStream *s = (CallbackInStream*) p;
  SRes res;
  if (compress_all_at_once) {
    // Read whole buffer at the first call
    res  =  s->first_read? s->callback ("read", buf, *size, s->auxdata)
                         : 0;
    s->first_read = FALSE;
  } else {
    // Read data by the BUFFER_SIZE chunks
    res  =  s->callback ("read", buf, mymin(*size, BUFFER_SIZE), s->auxdata);
  }
  if (res >= 0)  {*size = res; return SZ_OK;}
  else           {*size = 0;   s->errcode = res; return res;}
}

size_t CallbackWrite(void *p, const void *buf, size_t size)
{
  CallbackOutStream *s = (CallbackOutStream*) p;
  int res = s->callback ("write", (void*)buf, size, s->auxdata);
  if (res < 0) {s->errcode = res; return 0;}
  else         return size;
}



int lzma_compress  ( int dictionarySize,
                     int hashSize,
                     int algorithm,
                     int numFastBytes,
                     int matchFinder,
                     int matchFinderCycles,
                     int posStateBits,
                     int litContextBits,
                     int litPosBits,
                     CALLBACK_FUNC *callback,
                     void *auxdata )
{
  CallbackInStream  inStream;    inStream.Read  = CallbackRead;    inStream.callback = callback;   inStream.auxdata = auxdata;   inStream.errcode = FREEARC_OK;  inStream.first_read = False;
  CallbackOutStream outStream;  outStream.Write = CallbackWrite;  outStream.callback = callback;  outStream.auxdata = auxdata;  outStream.errcode = FREEARC_OK;

  CLzmaEncHandle enc;
  SRes res;
  CLzmaEncProps props;

  LzmaEncProps_Init(&props);
  props.dictSize = dictionarySize;
  props.mc = matchFinderCycles;
  props.lc = litContextBits;
  props.lp = litPosBits;
  props.pb = posStateBits;
  props.algo = algorithm;
  props.fb = numFastBytes;
  props.hashSize = hashSize;
  switch (matchFinder)
  {
    case kHC4:  props.btMode = MF_HashChain ;  props.numHashBytes = 4; break;
    case kBT2:  props.btMode = MF_BinaryTree;  props.numHashBytes = 2; break;
    case kBT3:  props.btMode = MF_BinaryTree;  props.numHashBytes = 3; break;
    case kBT4:  props.btMode = MF_BinaryTree;  props.numHashBytes = 4; break;
    case kHT4:  props.btMode = MF_HashTable;   props.numHashBytes = 4; break;
  }
  props.numThreads = GetCompressionThreads();
  props.writeEndMark = 1;
  LzmaEncProps_Normalize(&props);

  enc = LzmaEnc_Create(&g_Alloc);
  if (enc == 0)   return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
  res = LzmaEnc_SetProps(enc, &props);
  if (res == SZ_OK)
    res = LzmaEnc_Encode(enc, (ISeqOutStream*)&outStream, (ISeqInStream*)&inStream, NULL, &g_Alloc, &g_Alloc);
  LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);

  // Вернуть код ошибки входного/выходного потока или перекодировать код ошибки из 7z в fa
  if (inStream.errcode)
    return inStream.errcode;
  if (outStream.errcode)
    return outStream.errcode;
  return SRes_to_FreeArc(res);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)


int lzma_decompress( int dictionarySize,
                     int hashSize,
                     int algorithm,
                     int numFastBytes,
                     int matchFinder,
                     int matchFinderCycles,
                     int posStateBits,
                     int litContextBits,
                     int litPosBits,
                     CALLBACK_FUNC *callback,
                     void *auxdata )
{
  int errcode = FREEARC_OK;
  bool first_read = TRUE;

  UInt32 _inPos = 0, _inSize = 0, _inBufferSize = RangeDecoderBufferSize(dictionarySize);
  Byte  *_inBuf = (Byte*) MyAlloc(_inBufferSize);
  if (_inBuf == NULL)  return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;

  CLzmaProps LzmaProps;
  LzmaProps.pb = posStateBits;
  LzmaProps.lc = litContextBits;
  LzmaProps.lp = litPosBits;
  LzmaProps.dicSize = dictionarySize;

  CLzmaDec _state;
  LzmaDec_Construct(&_state);
  SRes res = LzmaDec_AllocateUsingProperties(&_state, LzmaProps, &g_Alloc);
  if (res != SZ_OK)  {errcode = SRes_to_FreeArc(res); goto freeInBuf;}
  LzmaDec_Init(&_state);

  for (;;)
  {
    if (_inPos == _inSize)
    {
      // Read whole buffer in the first call if compress_all_at_once==TRUE
      _inSize = errcode = first_read || !compress_all_at_once? callback ("read", _inBuf, _inBufferSize, auxdata) : 0;
      if (errcode < 0)  break;
      _inPos = 0;              // current position inside input data
      first_read = FALSE;
    }

    SizeT oldDicPos = _state.dicPos;
    SizeT curSize = mymin(_state.dicBufSize - oldDicPos, LARGE_BUFFER_SIZE);     // Write outdata in 256kb chunks

    ELzmaFinishMode finishMode = LZMA_FINISH_ANY;
    SizeT inSizeProcessed = _inSize - _inPos;
    ELzmaStatus status;
    SRes res = LzmaDec_DecodeToDic(&_state, oldDicPos + curSize, _inBuf + _inPos, &inSizeProcessed, finishMode, &status);

    _inPos += (UInt32)inSizeProcessed;
    SizeT outSizeProcessed = _state.dicPos - oldDicPos;

    errcode = callback ("write", _state.dic+oldDicPos, _state.dicPos-oldDicPos, auxdata);
    if (res != 0)    {errcode = SRes_to_FreeArc(res); break;}
    if (errcode < 0)  break;

    bool finished = (inSizeProcessed == 0 && outSizeProcessed == 0);
    if (finished)  {errcode = SRes_to_FreeArc(status == LZMA_STATUS_FINISHED_WITH_MARK ? SZ_OK : SZ_ERROR_DATA); break;}

    if (_state.dicPos == _state.dicBufSize)
      _state.dicPos = 0;
  }

  LzmaDec_Free(&_state, &g_Alloc);
freeInBuf:
  MyFree(_inBuf);
  return errcode;
}


/*-------------------------------------------------*/
/* Реализация класса LZMA_METHOD                  */
/*-------------------------------------------------*/

// Если строка str начинается со start, то возвратить адрес остатка, иначе - NULL
char* start_from (char* str, char* start)
{
  while (*start && *str==*start)  str++, start++;
  return *start? NULL : str;
}

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
LZMA_METHOD::LZMA_METHOD()
{
  dictionarySize    = 64*mb;
  hashSize          = 0;
  algorithm         = 1;
  numFastBytes      = 32;
  matchFinder       = kHT4;
  matchFinderCycles = 0;    // библиотека LZMA определит количество циклов автоматически, исходя из matchFinder и numFastBytes
  posStateBits      = 2;
  litContextBits    = 3;
  litPosBits        = 0;
}

// Функция распаковки
int LZMA_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("lzma_decompress");
  if (!f) f = (FARPROC) lzma_decompress;

  return ((int (*)(int, int, int, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                         (dictionarySize,
                          hashSize,
                          algorithm,
                          numFastBytes,
                          matchFinder,
                          matchFinderCycles,
                          posStateBits,
                          litContextBits,
                          litPosBits,
                          callback,
                          auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int LZMA_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("lzma_compress");
  if (!f) f = (FARPROC) lzma_compress;

  SetDictionary (dictionarySize);   // Ограничим размер словаря чтобы сжатие влезало в 4гб памяти :)
  // Если LZMA будет использовать multithreading алгоритм,
  // то нет смысла считать время работы по основному треду - вместо этого
  // следует использовать wall clock time всего процесса упаковки
  if (algorithm && GetCompressionThreads()>1)
      addtime = -1;   // это сигнал на использование wall clock time
  return ((int (*)(int, int, int, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                         (dictionarySize,
                          hashSize,
                          algorithm,
                          numFastBytes,
                          matchFinder,
                          matchFinderCycles,
                          posStateBits,
                          litContextBits,
                          litPosBits,
                          callback,
                          auxdata);
}


// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_LZMA)
void LZMA_METHOD::ShowCompressionMethod (char *buf)
{
  char DictionaryStr[100], HashStr[100], fcStr[100], pbStr[100], lcStr[100], lpStr[100], algStr[100];
  showMem (dictionarySize, DictionaryStr);
  showMem (hashSize, HashStr);
  LZMA_METHOD defaults;
  sprintf (fcStr, matchFinderCycles!=defaults.matchFinderCycles? ":mc%d" : "", matchFinderCycles);
  sprintf (pbStr, posStateBits     !=defaults.posStateBits     ? ":pb%d" : "", posStateBits);
  sprintf (lcStr, litContextBits   !=defaults.litContextBits   ? ":lc%d" : "", litContextBits);
  sprintf (lpStr, litPosBits       !=defaults.litPosBits       ? ":lp%d" : "", litPosBits);
  matchFinder==kHT4? (algorithm==2? sprintf (algStr, "a%d", algorithm)
                                  : sprintf (algStr, algorithm==0? "fast" : "normal"))
                   : sprintf (algStr, "%s:%s", algorithm==0? "fast": algorithm==1? "normal": "max", kMatchFinderIDs [matchFinder]);
  for (char *p=algStr; *p; p++)   *p = tolower(*p);  // strlwr(algStr);
  sprintf (buf, "lzma:%s%s%s:%s:%d%s%s%s%s",
                      DictionaryStr,
                      hashSize? ":h" : "",
                      hashSize? HashStr : "",
                      algStr,
                      numFastBytes,
                      fcStr,
                      pbStr,
                      lcStr,
                      lpStr);
  //printf("\n%s\n",buf);
}

// Посчитать, сколько памяти требуется для упаковки заданным методом
MemSize LZMA_METHOD::GetCompressionMem (void)
{
  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  props.dictSize = dictionarySize;
  props.mc = matchFinderCycles;
  props.lc = litContextBits;
  props.lp = litPosBits;
  props.pb = posStateBits;
  props.algo = algorithm;
  props.fb = numFastBytes;
  props.hashSize = hashSize;
  switch (matchFinder)
  {
    case kHC4:  props.btMode = MF_HashChain ;  props.numHashBytes = 4; break;
    case kBT2:  props.btMode = MF_BinaryTree;  props.numHashBytes = 2; break;
    case kBT3:  props.btMode = MF_BinaryTree;  props.numHashBytes = 3; break;
    case kBT4:  props.btMode = MF_BinaryTree;  props.numHashBytes = 4; break;
    case kHT4:  props.btMode = MF_HashTable;   props.numHashBytes = 4; break;
  }
  props.numThreads = GetCompressionThreads();
  props.writeEndMark = 1;
  LzmaEncProps_Normalize(&props);

  uint64 reservedArea = props.dictSize/(matchFinder==kHT4? 4 : 2);
  uint64 sons         = matchFinder==kHT4? 0
                      : matchFinder==kHC4? 1
                      :                    2;
  // Возращаем значение не более 4gb-1
  return MemSize (mymin (MemSize(-1), uint64(props.dictSize) + reservedArea + props.hashSize + sons*sizeof(CLzRef)*props.dictSize + 1*mb));
}

// Вычисляет словарь, использующий не более mem памяти для сжатия заданным LZMA_METHOD
MemSize calcDictSize (LZMA_METHOD *p, MemSize mem)
{
  double mem4 = mymax (double(mem) - BUFFER_SIZE, 0);
  if (p->hashSize)
  {
    mem4 = mymax (mem4 - p->hashSize, 0);
    switch (p->matchFinder) {
      case kBT2:    return (MemSize)floor(mem4/9.5);
      case kBT3:    return (MemSize)floor(mem4/9.5);
      case kBT4:    return (MemSize)floor(mem4/9.5);
      case kHC4:    return (MemSize)floor(mem4/5.5);
      case kHT4:    return (MemSize)floor(mem4/1.25);
    }
  }
  else
  {
    switch (p->matchFinder) {
      case kBT2:    return (MemSize)floor(mem4/9.5);
      case kBT3:    return (MemSize)floor(mem4/11.5);
      case kBT4:    return (MemSize)floor(mem4/11.5);
      case kHC4:    return (MemSize)floor(mem4/7.5);
      case kHT4:    return (MemSize)floor(mem4/2.25);
    }
  }
  return 0;
}

// Ограничить использование памяти при упаковке
void LZMA_METHOD::SetCompressionMem (MemSize mem)
{
  if (mem<=0)  return;
  SetDictionary (calcDictSize (this, mem));
}

// Ограничить использование памяти при распаковке
void LZMA_METHOD::SetDecompressionMem (MemSize mem)
{
  if (mem<=0)  return;
  SetDictionary (mem);
}

// Установить размер словаря.
void LZMA_METHOD::SetDictionary (MemSize mem)
{
  if (mem<=0)  return;
  // Ограничим размер словаря чтобы сжатие влезало в 4гб памяти :)
  dictionarySize = mymin (mem, roundDown (calcDictSize (this, UINT_MAX), mb));
  // Словарь - минимум 32 кб, по возможности округлённый до мегабайт
  dictionarySize = mymax (32*kb, dictionarySize);
  dictionarySize = dictionarySize < 2*mb? dictionarySize
                                        : roundDown(dictionarySize, 1*mb);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

MemSize LZMA_METHOD::GetDecompressionMem (void)
{
  return dictionarySize + RangeDecoderBufferSize(dictionarySize);
}

// Конструирует объект типа LZMA_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_LZMA (char** parameters)
{
  if (strcmp (parameters[0], "lzma") == 0) {
    // Если название метода (нулевой параметр) - "lzma", то разберём остальные параметры

    LZMA_METHOD *p = new LZMA_METHOD;
    p->matchFinder = INT_MAX;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters) {
      char *param = *parameters;  bool optional = param[0]=='*';  optional && param++;
           if (start_from (param, "d"))    p->dictionarySize    = parseMem (param+1, &error);
      else if (start_from (param, "h"))   {MemSize h            = parseMem (param+1, &error);
                                           if (error)  goto unnamed;  else p->hashSize = h;}
      else if (start_from (param, "a"))    p->algorithm         = parseInt (param+1, &error);
      else if (start_from (param, "fb"))   p->numFastBytes      = parseInt (param+2, &error);
      else if (start_from (param, "mc"))   p->matchFinderCycles = parseInt (param+2, &error);
      else if (start_from (param, "lc"))   p->litContextBits    = parseInt (param+2, &error);
      else if (start_from (param, "lp"))   p->litPosBits        = parseInt (param+2, &error);
      else if (start_from (param, "pb"))   p->posStateBits      = parseInt (param+2, &error);
      else if (start_from (param, "mf"))   p->matchFinder       = FindMatchFinder (param[2]=='='? param+3 : param+2);
      else if (strequ (param, "fastest"))  p->algorithm = 0,  p->matchFinder==INT_MAX && (p->matchFinder = kHT4),  p->numFastBytes = 32,   p->matchFinderCycles = 1;
      else if (strequ (param, "fast"))     p->algorithm = 0,  p->matchFinder==INT_MAX && (p->matchFinder = kHT4),  p->numFastBytes = 32,   p->matchFinderCycles = 0;
      else if (strequ (param, "normal"))   p->algorithm = 1,  p->matchFinder==INT_MAX && (p->matchFinder = kHT4),  p->numFastBytes = 32,   p->matchFinderCycles = 0;
      else if (strequ (param, "max"))      p->algorithm = 1,  p->matchFinder==INT_MAX && (p->matchFinder = kBT4),  p->numFastBytes = 128,  p->matchFinderCycles = 0;
      else if (strequ (param, "ultra"))    p->algorithm = 1,  p->matchFinder==INT_MAX && (p->matchFinder = kBT4),  p->numFastBytes = 128,  p->matchFinderCycles = 128;
      else {
unnamed:// Сюда мы попадаем, если в параметре опущено его наименование
        // Это может быть строка - имя MatchFinder'а, целое число - значение numFastBytes,
        // или обозначение памяти - значение dictionarySize
        error = 0;
        int n = FindMatchFinder (param);
        if (n>=0)
          p->matchFinder = n;
        else {
          n = parseInt (param, &error);
          if (!error)  p->numFastBytes = n;
          else         {error=0; MemSize n = parseMem (param, &error);
                        if (!error)  p->dictionarySize = n;}
        }
      }
      if (!optional)
        if (error || p->matchFinder<0)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    }
    if (p->matchFinder == INT_MAX)
      p->matchFinder = kHT4;   // default match finder
    return p;
  } else
    return NULL;   // Это не метод lzma
}

static int LZMA_x = AddCompressionMethod (parse_LZMA);   // Зарегистрируем парсер метода LZMA
