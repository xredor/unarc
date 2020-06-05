#include "../Compression.h"
#include "libGRZip.h"

int __cdecl grzip_compress   (int Method,
                      int BlockSize,
                      int EnableLZP,
                      int MinMatchLen,
                      int HashSizeLog,
                      int AlternativeBWTSort,
                      int AdaptiveBlockSize,
                      int DeltaFilter,
                      CALLBACK_FUNC *callback,
                      void *auxdata);

int __cdecl grzip_decompress (CALLBACK_FUNC *callback,
                      void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class GRZIP_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  MemSize BlockSize;        // Размер блока данных, обрабатываемых совместно
  int     Method;
  int     EnableLZP;
  int     MinMatchLen;
  int     HashSizeLog;
  int     AlternativeBWTSort;
  int     AdaptiveBlockSize;
  int     DeltaFilter;

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  GRZIP_METHOD();

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_GRZIP)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return BlockSize*9*GetCompressionThreads();}
  virtual MemSize GetDictionary         (void)         {return BlockSize;}
  virtual MemSize GetBlockSize          (void)         {return BlockSize;}
  virtual void    SetCompressionMem     (MemSize mem)  {SetBlockSize (mem/9/GetCompressionThreads());}
  virtual void    SetDecompressionMem   (MemSize mem)  {SetBlockSize (mem/5/GetCompressionThreads());}
  virtual void    SetDictionary         (MemSize dict) {SetBlockSize (dict);}
  virtual void    SetBlockSize          (MemSize bs);
#endif
  virtual MemSize GetDecompressionMem   (void)         {return BlockSize*5*GetCompressionThreads();}
};

// Разборщик строки метода сжатия GRZIP
COMPRESSION_METHOD* parse_GRZIP (char** parameters);

#endif  // __cplusplus
