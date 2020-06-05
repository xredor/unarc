extern "C" {
#include "C_REP.h"
}


#define REP_LIBRARY
#include "rep.cpp"

/*-------------------------------------------------*/
/* Реализация класса REP_METHOD                    */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
REP_METHOD::REP_METHOD()
{
  BlockSize      = 64*mb;
  MinCompression = 100;
  MinMatchLen    = 512;
  HashSizeLog    = 0;
  Barrier        = INT_MAX;
  SmallestLen    = 512;
  Amplifier      = 1;
}

// Функция распаковки
int REP_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("rep_decompress");
  if (!f) f = (FARPROC) rep_decompress;

  return ((int (__cdecl *)(MemSize, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                          (BlockSize, MinCompression, MinMatchLen, Barrier, SmallestLen, HashSizeLog, Amplifier, callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int REP_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("rep_compress");
  if (!f) f = (FARPROC) rep_compress;

  return ((int (__cdecl *)(MemSize, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                          (BlockSize, MinCompression, MinMatchLen, Barrier, SmallestLen, HashSizeLog, Amplifier, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_REP)
void REP_METHOD::ShowCompressionMethod (char *buf)
{
  REP_METHOD defaults; char BlockSizeStr[100], MinCompressionStr[100], BarrierTempStr[100], BarrierStr[100], SmallestLenStr[100], HashSizeLogStr[100], AmplifierStr[100], MinMatchLenStr[100];
  showMem (BlockSize, BlockSizeStr);
  showMem (Barrier,   BarrierTempStr);
  sprintf (MinCompressionStr, MinCompression!=defaults.MinCompression? ":%d%%" : "", MinCompression);
  sprintf (BarrierStr,     Barrier    !=defaults.Barrier    ? ":d%s" : "", BarrierTempStr);
  sprintf (SmallestLenStr, SmallestLen!=defaults.SmallestLen? ":s%d" : "", SmallestLen);
  sprintf (AmplifierStr,   Amplifier  !=defaults.Amplifier  ? ":a%d" : "", Amplifier);
  sprintf (HashSizeLogStr, HashSizeLog!=defaults.HashSizeLog? ":h%d" : "", HashSizeLog);
  sprintf (MinMatchLenStr, MinMatchLen!=defaults.MinMatchLen? ":%d"  : "", MinMatchLen);
  sprintf (buf, "rep:%s%s%s%s%s%s%s", BlockSizeStr, MinCompressionStr, MinMatchLenStr, BarrierStr, SmallestLenStr, HashSizeLogStr, AmplifierStr);
}

// Посчитать, сколько памяти требуется для упаковки заданным методом
MemSize REP_METHOD::GetCompressionMem (void)
{
  // Скопировано из rep_compress
  int L = roundup_to_power_of (mymin(SmallestLen,MinMatchLen)/2, 2);  // Размер блоков, КС которых заносится в хеш
  int k = sqrtb(L*2);
  int HashSize = CalcHashSize (HashSizeLog, BlockSize, k);

  return BlockSize + HashSize*sizeof(int);
}

// Посчитать, сколько памяти требуется для упаковки заданным методом
void REP_METHOD::SetCompressionMem (MemSize mem)
{
  if (mem>0)
  {
    // Скопировано из rep_compress
    int L = roundup_to_power_of (mymin(SmallestLen,MinMatchLen)/2, 2);  // Размер блоков, КС которых заносится в хеш
    int k = sqrtb(L*2);
    int HashSize = CalcHashSize (HashSizeLog, mem/5*4, k);

    BlockSize = mem - HashSize*sizeof(int);
  }
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа REP_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_REP (char** parameters)
{
  if (strcmp (parameters[0], "rep") == 0) {
    // Если название метода (нулевой параметр) - "rep", то разберём остальные параметры

    REP_METHOD *p = new REP_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'b':  p->BlockSize   = parseMem (param+1, &error); continue;
        case 'l':  p->MinMatchLen = parseInt (param+1, &error); continue;
        case 'd':  p->Barrier     = parseMem (param+1, &error); continue;
        case 's':  p->SmallestLen = parseInt (param+1, &error); continue;
        case 'h':  p->HashSizeLog = parseInt (param+1, &error); continue;
        case 'a':  p->Amplifier   = parseInt (param+1, &error); continue;
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
    return NULL;   // Это не метод REP
}

static int REP_x = AddCompressionMethod (parse_REP);   // Зарегистрируем парсер метода REP
