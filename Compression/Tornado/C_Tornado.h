#include "../Compression.h"

int tor_compress   (PackMethod m, CALLBACK_FUNC *callback, void *auxdata);
int tor_decompress (CALLBACK_FUNC *callback, void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class TORNADO_METHOD : public COMPRESSION_METHOD
{
public:
  struct PackMethod m;      // Параметры этого метода сжатия

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  TORNADO_METHOD();
  // Универсальный метод: даём положительный ответ на запросы "VeryFast?" для режимов сжатия 1-4
  virtual int doit (char *what, int param, void *data, CALLBACK_FUNC *callback)
  {
      if (strequ (what,"VeryFast?"))  return m.hash_row_width<=2;
      else return COMPRESSION_METHOD::doit (what, param, data, callback);
  }

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_TORNADO)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return m.hashsize + m.buffer + tornado_compressor_outbuf_size(m.buffer);}
  virtual MemSize GetDictionary         (void)         {return m.buffer;}
  virtual MemSize GetBlockSize          (void)         {return 0;}
  virtual void    SetCompressionMem     (MemSize mem)  {if (mem>0)   m.hashsize = 1<<lb(mem/3), m.buffer=mem-m.hashsize;}
  virtual void    SetDecompressionMem   (MemSize mem)  {SetDictionary (mem);}
  virtual void    SetDictionary         (MemSize dict);
  virtual void    SetBlockSize          (MemSize bs)   {}
#endif
  virtual MemSize GetDecompressionMem   (void)         {return m.buffer;}
};

// Разборщик строки метода сжатия TORNADO
COMPRESSION_METHOD* parse_TORNADO (char** parameters);

#endif  // __cplusplus
