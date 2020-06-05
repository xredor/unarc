/* Quick&dirty LZP compresion algorithm, developed by Dmitry Shkarin.
   Original code: http://www.compression.ru/ds/lzp.rar
   In turn, this code is based on LZP preprocessor in GRZipII compression
     algorithm, developed by Ilya Grebnov, Ilya.Grebnov@magicssoft.ru.
   Original code: http://magicssoft.ru/content/download/GRZipII/GRZipIISRC.zip
*/

extern "C" {
#include "C_LZP.h"
}


/* 32-bit Rotates */
#if defined(FREEARC_WIN)

/* intrinsic rotate */
#include <stdlib.h>
#pragma intrinsic(_lrotr,_lrotl)
#define ROR(x,n) _lrotr(x,n)

#elif !defined(__STRICT_ANSI__) && defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)) && !defined(INTEL_CC) && !defined(LTC_NO_ASM)

static inline unsigned ROR(unsigned word, int i)
{
   asm ("rorl %%cl,%0"
      :"=r" (word)
      :"0" (word),"c" (i));
   return word;
}

#else

/* rotates the hard way */
#define ROR(x, y) ( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | ((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)

#endif


/*------------------------------------------------------------------------------------*/
/* Методы упаковки/распаковки, получающие и возвращающие данные через буфера в памяти */
/*------------------------------------------------------------------------------------*/

/*                          tuned for PPMd
static const BYTE MO2MML[4] = {5,11,19,44};
static inline UINT GetMinMatchLen(UINT MaxOrder) {
    return (MaxOrder < 6)?(MO2MML[MaxOrder-2]):(CLAMP(10*MaxOrder-15,51,475));
}
*/
enum { LZP_MATCH_FLAG=0xB5 };

static inline UINT& lzpC(BYTE* p) { return *(UINT*)(p-4); }
static inline UINT  lzpH(UINT c,BYTE* p,int HashMask) {
//    return (c+11*(c >> 15)+13*lzpC(p-1)) & HashMask;
    return (c+5*ROR(c,17)+3*lzpC(p-1)) & HashMask;
}
#define LZP_INIT(HashSize,Pattern)                                               \
    UINT i, k, n1=1, n=1, HashMask=HashSize-1;                                   \
    BYTE *p, *InEnd=In+Size, *OutStart=Out;                                      \
    BYTE **HTable = (BYTE**) BigAlloc (HashSize * sizeof(BYTE*));                  \
    if (HTable==NULL)  return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;                 \
    for (i=0;i < HashSize;i++)              HTable[i]=Pattern+5;                 \
    lzpC(Out+4)=lzpC(In+4);                 lzpC(Out+8)=lzpC(In+8);              \
    i=lzpC(Out += 12)=lzpC(In += 12);       k=lzpH(i,Out,HashMask);

#ifndef FREEARC_DECOMPRESS_ONLY
int LZPEncode(BYTE* In,UINT Size,BYTE* Out,int MinLen,int HashSize,int Barrier,int SmallestLen)
{
    BYTE* OutEnd=Out+Size;   if (Size<32)  return 0;
    LZP_INIT(HashSize,In);
    do {
        p=HTable[k];                        int ml;
        if ( !--n )  { HTable[k]=In;        n=n1; }
        if (i != lzpC(p))                   *Out++ = *In++;
        else if ((ml = In-p>Barrier? SmallestLen:MinLen), (In+ml <= InEnd && lzpC(p+ml) == lzpC(In+ml))) {
            for (i=4;In+i <= InEnd && lzpC(p+i) == lzpC(In+i);i += 4)
                    ;
            for (i -= 4;In+i < InEnd && In[i] == p[i];i++)
                    ;
            if (i < ml)                     goto MATCH_NOT_FOUND;
            HTable[k]=In;                   n1 += (In-p > (n1+1)*HashSize && n1 < 7);
            *Out++ = LZP_MATCH_FLAG;        In += (k=i);
            for (i -= ml;i>=254 && Out<OutEnd;i -= 254)
                    *--OutEnd = 0;
            *--OutEnd = i+1;
            while(int(k -= 2*n1+1) > 0)     HTable[lzpH(lzpC(In-k),In-k,HashMask)]=In-k;
        } else {
MATCH_NOT_FOUND:
            if ((*Out++ = *In++) == LZP_MATCH_FLAG)
                    *--OutEnd = 255;
        }
        k=lzpH(i=lzpC(In),In,HashMask);
    } while (In<InEnd && Out<OutEnd);
    BigFree(HTable);
    if (Out >= OutEnd)       return 0;
    memmove(Out,OutEnd,OutStart+Size-OutEnd);
    return Size-(OutEnd-Out);
}
#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

int LZPDecode(BYTE* In,UINT Size,BYTE* Out,int MinLen,int HashSize,int Barrier,int SmallestLen)
{
    LZP_INIT(HashSize,Out);
    do {
        p=HTable[k];
        if ( !--n )  { HTable[k]=Out;       n=n1; }
        if (*In++ != LZP_MATCH_FLAG || i != lzpC(p) || *--InEnd == 255)
                *Out++ = In[-1];
        else {
            HTable[k]=Out;                  n1 += (Out-p > (n1+1)*HashSize && n1 < 7);
            for (i=(Out-p>Barrier? SmallestLen:MinLen)-1;*InEnd == 0;InEnd--)
                    i += 254;
            i += *InEnd;                    k=2*n1+2;
            do {
                if ( !--k ) { k=2*n1+1;     HTable[lzpH(lzpC(Out),Out,HashMask)]=Out; }
                *Out++ = *p++;
            } while ( --i );
        }
        k=lzpH(i=lzpC(Out),Out,HashMask);
    } while (In < InEnd);
    BigFree(HTable);
    return (Out-OutStart);
}



/*-------------------------------------------------------------------------*/
/* Методы упаковки/распаковки, использующие callbacks для ввода/вывода     */
/*-------------------------------------------------------------------------*/

#ifndef FREEARC_DECOMPRESS_ONLY
int lzp_compress (MemSize BlockSize, int MinCompression, int MinMatchLen, int HashSizeLog, int Barrier, int SmallestLen, CALLBACK_FUNC *callback, void *auxdata)
{
    int errcode = FREEARC_OK;   // Error code returned by last operation or FREEARC_OK
    BYTE* In = NULL;  // указатель на входные данные
    BYTE* Out= NULL;  // указатель на выходные данные
    while (1)
    {
        int InSize, OutSize;     // количество байт во входном и выходном буфере, соответственно
        BIGALLOC (BYTE, In, BlockSize+2);
    	READ_LEN_OR_EOF (InSize, In, BlockSize);
        //In = (BYTE*) realloc(In,InSize);   -- impossible since we used BigAlloc
        BIGALLOC (BYTE, Out, InSize+2);
        OutSize = LZPEncode (In, InSize, Out, MinMatchLen, 1<<HashSizeLog, Barrier, SmallestLen);
        if (OutSize<0)  {errcode=OutSize; goto finished;}
        if (OutSize==0 || MinCompression>0 && OutSize/MinCompression>=InSize/100) {
            // Упаковать данные [достаточно хорошо] не удалось, запишем вместо них исходные данные
            BigFreeAndNil(Out);
            WRITE4 (-InSize);      // Отрицательное число в качестве длины блока - признак Stored блока
            WRITE  (In, InSize);
            BigFreeAndNil(In);
        } else {
            // Данные успешно упакованы, можно освободить входной буфер прежде чем записывать их
            // (чтобы освободить больше памяти для следующего алгоритма в цепочке алгоритмов сжатия)
            BigFreeAndNil(In);
            WRITE4 (OutSize);
            WRITE  (Out, OutSize);
            BigFreeAndNil(Out);
        }
    }
finished:
    BigFreeAndNil(In); BigFreeAndNil(Out);
    return errcode;
}
#endif  // !defined (FREEARC_DECOMPRESS_ONLY)


int lzp_decompress (MemSize BlockSize, int MinCompression, int MinMatchLen, int HashSizeLog, int Barrier, int SmallestLen, CALLBACK_FUNC *callback, void *auxdata)
{
    int errcode = FREEARC_OK;   // Error code returned by last operation or FREEARC_OK
    BYTE* In = NULL;  // указатель на входные данные
    BYTE* Out= NULL;  // указатель на выходные данные
    for(;;) {
        int InSize, OutSize;     // количество байт во входном и выходном буфере, соответственно
        READ4_OR_EOF (InSize);
        if (InSize<0) {
            // скопируем неупакованные данные
            InSize = -InSize;
            BIGALLOC (BYTE, In, InSize);
            READ  (In, InSize);
            WRITE (In, InSize);
            BigFreeAndNil(In);
        } else {
            // Произвести декодирование и получить размер выходных данных
            BIGALLOC (BYTE, In,  InSize);
            BIGALLOC (BYTE, Out, BlockSize);
            READ  (In, InSize);
            OutSize = LZPDecode (In, InSize, Out, MinMatchLen, 1<<HashSizeLog, Barrier, SmallestLen);
            BigFreeAndNil(In);
            //Out = (BYTE*) realloc (Out, OutSize);   -- impossible since we used BigAlloc
            WRITE (Out, OutSize);
            BigFreeAndNil(Out);
        }
    }
finished:
    BigFreeAndNil(In); BigFreeAndNil(Out);
    return errcode;
}


/*-------------------------------------------------*/
/* Реализация класса LZP_METHOD                    */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
LZP_METHOD::LZP_METHOD()
{
  BlockSize      = 8*mb;
  MinCompression = 100;
  MinMatchLen    = 64;
  HashSizeLog    = 18;
  Barrier        = INT_MAX;
  SmallestLen    = 32;
}

// Функция распаковки
int LZP_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("lzp_decompress");
  if (!f) f = (FARPROC) lzp_decompress;

  return ((int (*)(MemSize, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
            (BlockSize, MinCompression, MinMatchLen, HashSizeLog, Barrier, SmallestLen, callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int LZP_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("lzp_compress");
  if (!f) f = (FARPROC) lzp_compress;

  return ((int (*)(MemSize, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
            (BlockSize, MinCompression, MinMatchLen, HashSizeLog, Barrier, SmallestLen, callback, auxdata);
}

// Установить размер блока и уменьшить размер хэша, если он слишком велик для такого маленького блока
void LZP_METHOD::SetBlockSize (MemSize bs)
{
  if (bs>0) {
    BlockSize   = bs;
    HashSizeLog = mymin (HashSizeLog, 1+lb(BlockSize-1));
  }
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_LZP)
void LZP_METHOD::ShowCompressionMethod (char *buf)
{
    LZP_METHOD defaults; char BlockSizeStr[100], MinCompressionStr[100], BarrierTempStr[100], BarrierStr[100], SmallestLenStr[100];
    showMem (BlockSize, BlockSizeStr);
    showMem (Barrier,   BarrierTempStr);
    sprintf (MinCompressionStr, MinCompression!=defaults.MinCompression? ":%d%%" : "", MinCompression);
    sprintf (BarrierStr, Barrier!=defaults.Barrier? ":d%s" : "", BarrierTempStr);
    sprintf (SmallestLenStr, SmallestLen!=defaults.SmallestLen? ":s%d" : "", SmallestLen);
    sprintf (buf, "lzp:%s%s:%d:h%d%s%s", BlockSizeStr, MinCompressionStr, MinMatchLen, HashSizeLog, BarrierStr, SmallestLenStr);
}

// Устанавливает количество памяти, которое должно использоваться при упаковке и распаковке
void LZP_METHOD::SetCompressionMem (MemSize mem)
{
  MemSize hashsize = (1<<HashSizeLog) * sizeof(BYTE*);
  // Если хеш занимает слишком много места - укоротим сначала его. Этого может оказаться достаточно
  if (hashsize > mem/4) {
    HashSizeLog = lb(mem/16);
    if (GetCompressionMem() <= mem)  return;
    hashsize = (1<<HashSizeLog) * sizeof(BYTE*);
  }
  SetBlockSize ((mem-hashsize)/2);
}


#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа LZP_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_LZP (char** parameters)
{
  if (strcmp (parameters[0], "lzp") == 0) {
    // Если название метода (нулевой параметр) - "lzp", то разберём остальные параметры

    LZP_METHOD *p = new LZP_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'b':  p->BlockSize   = parseMem (param+1, &error); continue;
        case 'l':  p->MinMatchLen = parseInt (param+1, &error); continue;
        case 'h':  p->HashSizeLog = parseInt (param+1, &error); continue;
        case 'd':  p->Barrier     = parseMem (param+1, &error); continue;
        case 's':  p->SmallestLen = parseInt (param+1, &error); continue;
      }
      // Если параметр заканчивается знаком процента. то попробуем распарсить его как "N%"
      if (last_char(param) == '%') {
        char str[100]; strcpy(str,param); last_char(str) = '\0';
        int n = parseInt (str, &error);
        if (!error) { p->MinCompression = n; continue; }
        error=0;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как целое число (т.е. в нём - только цифры),
      // то присвоим его значение полю MinMatchLen, иначе попробуем разобрать его как BlockSize
      int n = parseInt (param, &error);
      if (!error) p->MinMatchLen = n;
      else        error=0, p->BlockSize = parseMem (param, &error);
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод lzp
}

static int LZP_x = AddCompressionMethod (parse_LZP);   // Зарегистрируем парсер метода LZP
