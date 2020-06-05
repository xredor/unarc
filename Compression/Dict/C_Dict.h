#include "../Compression.h"

int dict_compress   (MemSize BlockSize, int MinCompression, int MinWeakChars, int MinLargeCnt, int MinMediumCnt, int MinSmallCnt, int MinRatio, CALLBACK_FUNC *callback, void *auxdata);
int dict_decompress (MemSize BlockSize, int MinCompression, int MinWeakChars, int MinLargeCnt, int MinMediumCnt, int MinSmallCnt, int MinRatio, CALLBACK_FUNC *callback, void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class DICT_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  MemSize BlockSize;        // Размер блока данных, обрабатываемого за раз. Каждый блок получает свой собственный словарь
  int     MinCompression;   // Минимальный процент сжатия. Если выходные данные больше, то вместо них будут записаны оригинальные (несжатые) данные
  int     MinWeakChars;     // Минимально допустимое количество weak chars. Если оно окажется меньше - произойдёт отказ от сжатия, поскольку малые значения weak chars обычно свидетельствуют о том, что это бинарный файл, который этому алгоритму сжать не удастся
  int     MinLargeCnt;      // Минимальный "большой" счётчик
  int     MinMediumCnt;     // Минимальный "средний" счётчик
  int     MinSmallCnt;      // Минимальный "маленький" счётчик
  int     MinRatio;         // Минимальная "пропорция"

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  DICT_METHOD();

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_DICT)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return BlockSize*2;}
  virtual MemSize GetDictionary         (void)         {return BlockSize;}
  virtual MemSize GetBlockSize          (void)         {return BlockSize;}
  virtual void    SetCompressionMem     (MemSize mem)  {if (mem>0)   BlockSize = mem/2;}
  virtual void    SetDecompressionMem   (MemSize mem)  {if (mem>0)   BlockSize = mem/2;}
  virtual void    SetDictionary         (MemSize dict) {if (dict>0)  BlockSize = dict;}
  virtual void    SetBlockSize          (MemSize bs)   {if (bs>0)    BlockSize = bs;}
#endif
  virtual MemSize GetDecompressionMem   (void)         {return 1*mb /*BlockSize*2*/;}
};

// Разборщик строки препроцессора DICT
COMPRESSION_METHOD* parse_DICT (char** parameters);

#endif  // __cplusplus
