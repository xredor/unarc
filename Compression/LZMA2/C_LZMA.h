#include "../Compression.h"

int lzma_compress   (int dictionarySize,
                     int hashSize,
                     int algorithm,
                     int numFastBytes,
                     int matchFinder,
                     int matchFinderCycles,
                     int posStateBits,
                     int litContextBits,
                     int litPosBits,
                     CALLBACK_FUNC *callback,
                     void *auxdata);

int lzma_decompress (int dictionarySize,
                     int hashSize,
                     int algorithm,
                     int numFastBytes,
                     int matchFinder,
                     int matchFinderCycles,
                     int posStateBits,
                     int litContextBits,
                     int litPosBits,
                     CALLBACK_FUNC *callback,
                     void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class LZMA_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  MemSize dictionarySize;
  MemSize hashSize;
  int     algorithm;
  int     numFastBytes;
  int     matchFinder;
  int     matchFinderCycles;
  int     posStateBits;
  int     litContextBits;
  int     litPosBits;

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  LZMA_METHOD();

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_LZMA)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void);
  virtual MemSize GetDictionary         (void)         {return dictionarySize;}
  virtual MemSize GetBlockSize          (void)         {return 0;}
  virtual void    SetCompressionMem     (MemSize mem);
  virtual void    SetDecompressionMem   (MemSize mem);
  virtual void    SetDictionary         (MemSize dict);
  virtual void    SetBlockSize          (MemSize)      {}
#endif
  virtual MemSize GetDecompressionMem   (void);
};

// Разборщик строки метода сжатия LZMA
COMPRESSION_METHOD* parse_LZMA (char** parameters);

#endif  // __cplusplus
