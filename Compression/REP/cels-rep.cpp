#include "../CELS.h"

namespace CELS
{

#define REP_LIBRARY
#include "rep.cpp"

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
struct REP_METHOD : COMPRESSION_METHOD
{
  // Параметры этого метода сжатия
  MemSize BlockSize;        // Размер буфера. Совпадения ищутся только в пределах этой дистанции. Расход памяти - BlockSize+BlockSize/4
  int     MinCompression;   // Минимальный процент сжатия. Если выходные данные больше, то вместо них будут записаны оригинальные (несжатые) данные
  int     MinMatchLen;      // Минимальная длина строки, при которой она будет заменяться ссылкой на предыдущее вхождение
  int     Barrier;          // Граница, после которой допускается использовать совпадения меньшего размера (поскольку lzma/ppmd всё равно пропустит их)
  int     SmallestLen;      // Этот меньший размер
  int     HashSizeLog;      // Логарифм размера хеша (в 4-байтовых словах). Большие значения увеличивают сжатие, но замедляют его. При нулевом значении оптимальный размер вычисляется автоматически
  int     Amplifier;        // Коэффициент "усиления" поиска

  // Конструктор, присваивающий параметрам метода значения по умолчанию
  REP_METHOD (TABI_ELEMENT* params) : COMPRESSION_METHOD(params)
  {
    BlockSize      = 64*mb;
    MinCompression = 100;
    MinMatchLen    = 512;
    Barrier        = INT_MAX;
    SmallestLen    = 512;
    HashSizeLog    = 0;
    Amplifier      = 1;
  }

  // Функция распаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata)
  {
    return rep_decompress (BlockSize, MinCompression, MinMatchLen, Barrier, SmallestLen, HashSizeLog, Amplifier, callback, auxdata);
  }

#ifndef FREEARC_DECOMPRESS_ONLY
  // Функция упаковки
  virtual int compress (CALLBACK_FUNC *callback, void *auxdata)
  {
    return rep_compress (BlockSize, MinCompression, MinMatchLen, Barrier, SmallestLen, HashSizeLog, Amplifier, callback, auxdata);
  }

  // Разбирает строку с параметрами метода
  virtual void parse_method()
  {
    // Превратим строку метода сжатия в массив строк `parameters`, хранящий его название и параметры
    char* method = p._str("method");
    char* params [MAX_PARAMETERS], **parameters = params;
    char  local_method [MAX_METHOD_STRLEN];
    strncopy (local_method, method, sizeof (local_method));
    split (local_method, COMPRESSION_METHOD_PARAMETERS_DELIMITER, parameters, MAX_PARAMETERS);
    if (!strequ (parameters[0], "rep"))  throw "REP_METHOD:parse_method";

    // Если название метода (нулевой параметр) - "rep", то разберём остальные параметры
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'b':  BlockSize   = parseMem (param+1, &error); continue;
        case 'l':  MinMatchLen = parseInt (param+1, &error); continue;
        case 'd':  Barrier     = parseMem (param+1, &error); continue;
        case 's':  SmallestLen = parseInt (param+1, &error); continue;
        case 'h':  HashSizeLog = parseInt (param+1, &error); continue;
        case 'a':  Amplifier   = parseInt (param+1, &error); continue;
      }
      // Если параметр заканчивается знаком процента. то попробуем распарсить его как "N%"
      if (last_char(param) == '%') {
        char str[100]; strcpy(str,param); last_char(str) = '\0';
        int n = parseInt (str, &error);
        if (!error) { MinCompression = n; continue; }
        error=0;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как целое число (т.е. в нём - только цифры),
      // то присвоим его значение полю MinMatchLen, иначе попробуем разобрать его как BlockSize
      int n = parseInt (param, &error);
      if (!error) MinMatchLen = n;
      else        error=0, BlockSize = parseMem (param, &error);
    }
    if (error)  throw "rep:parse_method";  // Ошибка при парсинге параметров метода
  }

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_method)
  virtual void ShowCompressionMethod (char *buf)
  {
    REP_METHOD defaults(NULL); char BlockSizeStr[100], MinCompressionStr[100], BarrierTempStr[100], BarrierStr[100], SmallestLenStr[100], HashSizeLogStr[100], AmplifierStr[100], MinMatchLenStr[100];
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

  // Настроить метод сжатия на использование заданного объёма памяти
  virtual void SetCompressionMem (MemSize mem)
  {
    // Скопировано из rep_compress
    int L = roundup_to_power_of (mymin(SmallestLen,MinMatchLen)/2, 2);  // Размер блоков, КС которых заносится в хеш
    int k = sqrtb(L*2);
    int HashSize = CalcHashSize (HashSizeLog, mem/5*4, k);

    BlockSize = mem - HashSize*sizeof(int);
  }

  // Посчитать, сколько памяти требуется для упаковки заданным методом
  virtual MemSize GetCompressionMem()
  {
    // Скопировано из rep_compress
    int L = roundup_to_power_of (mymin(SmallestLen,MinMatchLen)/2, 2);  // Размер блоков, КС которых заносится в хеш
    int k = sqrtb(L*2);
    int HashSize = CalcHashSize (HashSizeLog, BlockSize, k);

    return BlockSize + HashSize*sizeof(int);
  }

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual void    SetDictionary       (MemSize dict) {BlockSize = dict;}
  virtual MemSize GetDictionary       (void)         {return BlockSize;}
  virtual void    SetDecompressionMem (MemSize mem)  {BlockSize = mem;}
#endif
  virtual MemSize GetDecompressionMem (void)         {return BlockSize;}
};

// Function that represents REP compression method
int rep_server (TABI_ELEMENT* params)
{
  return REP_METHOD(params).server();
}

// Register REP method in CELS
int rep_register = CELS_Register(rep_server);

}
