#include "../Compression.h"

int ppmd_compress   (int order, MemSize mem, int MRMethod, CALLBACK_FUNC *callback, void *auxdata);
int ppmd_decompress (int order, MemSize mem, int MRMethod, CALLBACK_FUNC *callback, void *auxdata);


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class PPMD_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  int     order;     // Порядок модели (по скольким последним сивмолам предсказывается следующий)
  MemSize mem;       // Объём памяти, используемой для хранения модели
  int     MRMethod;  // Что делать, когда память, выделенная для хранения модели, исчерпана

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  PPMD_METHOD();

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_PPMD)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)          {return mem;}
  virtual MemSize GetDictionary         (void)          {return 0;}
  virtual MemSize GetBlockSize          (void)          {return 0;}
  virtual void    SetCompressionMem     (MemSize _mem);
  virtual void    SetDecompressionMem   (MemSize _mem)  {SetCompressionMem(_mem);}
  virtual void    SetDictionary         (MemSize dict)  {}
  virtual void    SetBlockSize          (MemSize bs)    {}
#endif
  virtual MemSize GetDecompressionMem   (void)          {return mem;}
};

// Разборщик строки метода сжатия PPMD
COMPRESSION_METHOD* parse_PPMD (char** parameters);

#endif  // __cplusplus
