#include "../Compression.h"

int lzp_compress   (MemSize BlockSize, int MinCompression, int MinMatchLen, int HashSizeLog, int Barrier, int SmallestLen, CALLBACK_FUNC *callback, void *auxdata);
int lzp_decompress (MemSize BlockSize, int MinCompression, int MinMatchLen, int HashSizeLog, int Barrier, int SmallestLen, CALLBACK_FUNC *callback, void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class LZP_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  MemSize BlockSize;        // Размер блока, обрабатываемого за раз. Совпадения ищутся только внутри этого блока
  int     MinCompression;   // Минимальный процент сжатия. Если выходные данные больше, то вместо них будут записаны оригинальные (несжатые) данные
  int     MinMatchLen;      // Минимальный размер совпадающей строки, который будет сжиматься
  int     HashSizeLog;      // Логарифм размера хеша (в 4-байтовых словах). Большие значения увеличивают сжатие, но значительно замедляют его
  int     Barrier;          // Граница, после которой допускается использовать совпадения меньшего размера (поскольку lzma/ppmd всё равно пропустит их)
  int     SmallestLen;      // Строки какого размера допускаются если дистанция > Barrier

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  LZP_METHOD();
  // Универсальный метод: даём положительный ответ на запросы "VeryFast?" для хеша<=128кб
  virtual int doit (char *what, int param, void *data, CALLBACK_FUNC *callback)
  {
      if (strequ (what,"VeryFast?"))  return HashSizeLog<=15;
      else return COMPRESSION_METHOD::doit (what, param, data, callback);
  }

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_LZP)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return BlockSize*2 + (1<<HashSizeLog)*sizeof(BYTE*);}
  virtual MemSize GetDictionary         (void)         {return BlockSize;}
  virtual MemSize GetBlockSize          (void)         {return BlockSize;}
  virtual void    SetCompressionMem     (MemSize mem);
  virtual void    SetDecompressionMem   (MemSize mem)  {SetCompressionMem(mem);}
  virtual void    SetDictionary         (MemSize dict) {SetBlockSize (dict);}
  virtual void    SetBlockSize          (MemSize bs);
#endif
  virtual MemSize GetDecompressionMem   (void)         {return BlockSize*2 + (1<<HashSizeLog)*sizeof(BYTE*);}
};

// Разборщик строки метода сжатия LZP
COMPRESSION_METHOD* parse_LZP (char** parameters);

#endif  // __cplusplus
