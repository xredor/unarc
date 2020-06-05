#include "../Compression.h"

int delta_compress   (MemSize BlockSize, int ExtendedTables, CALLBACK_FUNC *callback, void *auxdata);
int delta_decompress (MemSize BlockSize, int ExtendedTables, CALLBACK_FUNC *callback, void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class DELTA_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  MemSize BlockSize;        // Размер блока данных, обрабатываемых за один раз
  int     ExtendedTables;   // Искать таблицы с размером элемента, отличным от 2/4

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  DELTA_METHOD();

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_DELTA)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return BlockSize;}
  virtual MemSize GetDictionary         (void)         {return 0;}
  virtual MemSize GetBlockSize          (void)         {return 0;}
  virtual void    SetCompressionMem     (MemSize mem)  {if (mem>0)   BlockSize = mem;}
  virtual void    SetDecompressionMem   (MemSize mem)  {if (mem>0)   BlockSize = mem;}
  virtual void    SetDictionary         (MemSize dict) {}
  virtual void    SetBlockSize          (MemSize bs)   {}
#endif
  virtual MemSize GetDecompressionMem   (void)         {return BlockSize;}
};

// Разборщик строки метода сжатия DELTA
COMPRESSION_METHOD* parse_DELTA (char** parameters);

#endif  // __cplusplus
